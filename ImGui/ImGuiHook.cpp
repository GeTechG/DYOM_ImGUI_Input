// ReSharper disable CppMemberFunctionMayBeStatic
// ReSharper disable CommentTypo
#include "ImGuiHook.h"

#include <utility>
#include "plugin.h"
#include "../VMTHooker/vmt.h"

#include <d3d9.h>
#include <cstring>
#include "CHud.h"
#include "CMessages.h"
#include "CMessages.h"
#include "CTheScripts.h"
#include "CWorld.h"
#include "ShlObj.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_impl_dx9.h"
#include "../ImGui/imgui_impl_win32.h"
#include "../ImGui/imgui_internal.h"

using namespace plugin;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LONG m_pWindowProc; // Переменная оригинального обработчика окна

VMTHookManager* vmtHooks;

typedef HRESULT(WINAPI* _Present)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
_Present oPresent;

typedef HRESULT(WINAPI* _Reset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
_Reset oReset;

void show_cursor(bool show)
{
	if (show) {
		patch::Nop(0x541DF5, 5); // don't call CControllerConfigManager::AffectPadFromKeyBoard
		patch::Nop(0x53F417, 5); // don't call CPad__getMouseState
		patch::SetRaw(0x53F41F, const_cast<char*>("\x33\xC0\x0F\x84"), 4); // test eax, eax -> xor eax, eax
														// jl loc_53F526 -> jz loc_53F526
		patch::PutRetn(0x6194A0); // disable RsMouseSetPos (ret)
		ImGui::GetIO().MouseDrawCursor = true;
		// (*(IDirect3DDevice9**)0xC97C28)->ShowCursor(true);

	}
	else {
		patch::SetRaw(0x541DF5, const_cast<char*>("\xE8\x46\xF3\xFE\xFF"), 5); // call CControllerConfigManager::AffectPadFromKeyBoard
		patch::SetRaw(0x53F417, const_cast<char*>("\xE8\xB4\x7A\x20\x00"), 5); // call CPad__getMouseState
		patch::SetRaw(0x53F41F, const_cast<char*>("\x85\xC0\x0F\x8C"), 4); // xor eax, eax -> test eax, eax
														// jz loc_53F526 -> jl loc_53F526
		patch::SetUChar(0x6194A0, 0xE9); // jmp setup
		// (*(IDirect3DDevice9**)0xC97C28)->ShowCursor(false);
		ImGui::GetIO().MouseDrawCursor = false;
		//ShowCursor(false);
	}

	(*reinterpret_cast<CMouseControllerState*>(0xB73418)).X = 0.0f;
	(*reinterpret_cast<CMouseControllerState*>(0xB73418)).Y = 0.0f;
	reinterpret_cast<void(*)()>(0x541BD0)(); // CPad::ClearMouseHistory
	reinterpret_cast<void(*)()>(0x541DD0)(); // CPad::UpdatePads
}

bool is_utf8(const char* string)
{
	if (!string)
		return false;

	const unsigned char* bytes = (const unsigned char*)string;
	while (*bytes)
	{
		if ((bytes[0] == 0x09 || bytes[0] == 0x0A || bytes[0] == 0x0D ||
			(0x20 <= bytes[0] && bytes[0] <= 0x7E)
			)
			)
		{
			bytes += 1;
			continue;
		}

		if (( // non-overlong 2-byte
			(0xC2 <= bytes[0] && bytes[0] <= 0xDF) &&
			(0x80 <= bytes[1] && bytes[1] <= 0xBF)
			)
			)
		{
			bytes += 2;
			continue;
		}

		if (( // excluding overlongs
			bytes[0] == 0xE0 &&
			(0xA0 <= bytes[1] && bytes[1] <= 0xBF) &&
			(0x80 <= bytes[2] && bytes[2] <= 0xBF)
			) ||
			( // straight 3-byte
				((0xE1 <= bytes[0] && bytes[0] <= 0xEC) ||
					bytes[0] == 0xEE ||
					bytes[0] == 0xEF) &&
				(0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF)
				) ||
			( // excluding surrogates
				bytes[0] == 0xED &&
				(0x80 <= bytes[1] && bytes[1] <= 0x9F) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF)
				)
			)
		{
			bytes += 3;
			continue;
		}

		if (( // planes 1-3
			bytes[0] == 0xF0 &&
			(0x90 <= bytes[1] && bytes[1] <= 0xBF) &&
			(0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
			(0x80 <= bytes[3] && bytes[3] <= 0xBF)
			) ||
			( // planes 4-15
				(0xF1 <= bytes[0] && bytes[0] <= 0xF3) &&
				(0x80 <= bytes[1] && bytes[1] <= 0xBF) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
				(0x80 <= bytes[3] && bytes[3] <= 0xBF)
				) ||
			( // plane 16
				bytes[0] == 0xF4 &&
				(0x80 <= bytes[1] && bytes[1] <= 0x8F) &&
				(0x80 <= bytes[2] && bytes[2] <= 0xBF) &&
				(0x80 <= bytes[3] && bytes[3] <= 0xBF)
				)
			)
		{
			bytes += 4;
			continue;
		}

		return false;
	}

	return true;
}

std::string UTF8_to_CP1251(std::string const& utf8)
{
	if (!utf8.empty())
	{
		if (!is_utf8(utf8.c_str()))
		{
			return utf8;
		}
		int wchlen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), NULL, 0);
		if (wchlen > 0 && wchlen != 0xFFFD)
		{
			std::vector<wchar_t> wbuf(wchlen);
			MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), utf8.size(), &wbuf[0], wchlen);
			std::vector<char> buf(wchlen);
			WideCharToMultiByte(1251, 0, &wbuf[0], wchlen, &buf[0], wchlen, 0, 0);

			return std::string(&buf[0], wchlen);
		}
	}
	return std::string();
}

std::string cp1251_to_utf8(const char* str)
{
	std::string res;
	WCHAR* ures = NULL;
	char* cres = NULL;

	int result_u = MultiByteToWideChar(1251, 0, str, -1, 0, 0);
	if (result_u != 0)
	{
		ures = new WCHAR[result_u];
		if (MultiByteToWideChar(1251, 0, str, -1, ures, result_u))
		{
			int result_c = WideCharToMultiByte(CP_UTF8, 0, ures, -1, 0, 0, 0, 0);
			if (result_c != 0)
			{
				cres = new char[result_c];
				if (WideCharToMultiByte(CP_UTF8, 0, ures, -1, cres, result_c, 0, 0))
				{
					res = cres;
				}
			}
		}
	}

	delete[] ures;
	delete[] cres;

	return res;
}

const char sym_ru[] = "АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯабвгдеёжзийклмнопрстуфхцчшщъыьэюя";
const char sym_sl[] = "AЂ‹‚ѓEE„€…†K‡–­OЊPCЏYЃX‰ЌЋЉђ‘’“”•a—ў™љee›џњќkћЇ®oЈpc¦yx ¤ҐЎ§Ё©Є«¬";
void GXTEncode(std::string& str, bool to_sl = true) {
	for (int i = 0; i < 66; i++) {
		std::string ru_utf8 = UTF8_to_CP1251(sym_ru);
		std::string sl_utf8 = UTF8_to_CP1251(sym_sl);
		if (to_sl)
			std::ranges::replace(str, ru_utf8[i], sl_utf8[i]);
		else
			std::ranges::replace(str, sl_utf8[i], ru_utf8[i]);

	}
}

std::string ReplaceAll(std::string str, const std::string& from, const std::string& to) {
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // Handles case where 'to' is a substring of 'from'
	}
	return str;
}

size_t                  /* O - Length of string */
strlcpy(char* dst,        /* O - Destination string */
	const char* src,      /* I - Source string */
	size_t      size)     /* I - Size of destination string buffer */
{
	size_t    srclen;         /* Length of source string */


   /*
	* Figure out how much room is needed...
	*/

	size--;

	srclen = strlen(src);

	/*
	 * Copy the appropriate amount...
	 */

	if (srclen > size)
		srclen = size;

	memcpy(dst, src, srclen);
	dst[srclen] = '\0';

	return (srclen);
}

size_t strlcat(char* destination, const char* source, size_t size)
{
	size_t len;

	for (len = 0; len < size; len++) {
		if ('\0' == destination[len]) {
			break;
		}
	}

	return len + strlcpy(destination + len, source, size - len);
}

bool Shortcut(ImGuiKeyModFlags mod, ImGuiKey key, bool repeat)
{
	return mod == ImGui::GetMergedKeyModFlags() && ImGui::IsKeyPressed(ImGui::GetKeyIndex(key), repeat);
}

static char buffer[200] = "";

#include <cstring>


void setData() {
	char* adress = *reinterpret_cast<char**>(CTheScripts::ScriptSpace + 9889 * 4);
	if (adress != nullptr) {
		std::string text = UTF8_to_CP1251(buffer);
		text = ReplaceAll(text, "\n", "~n~");
		text = ReplaceAll(text, "+", "~");
		GXTEncode(text);
		strlcpy(adress, text.c_str(), 100);
		CHud::SetHelpMessage(adress, false, true, false);
	}
}


void checkSnippet(ImGuiInputTextCallbackData* data) {
	auto &hook = ImGuiHook::getInstance();
	for (const auto& snippet : hook.getSnippets()) {
		const size_t lenSnippet = strlen(snippet.first.get());
		std::unique_ptr<char[]> eqSnipp(new char[16]);
		strcpy(eqSnipp.get(), buffer + data->CursorPos - lenSnippet);
		if (strcmp(snippet.first.get(), eqSnipp.get()) == 0) {
			data->DeleteChars(data->CursorPos - lenSnippet, lenSnippet);
			data->InsertChars(data->CursorPos, snippet.second.get());
			break;
		}
	}
}

inline void shortCut(ImGuiInputTextCallbackData* data) {
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_R, false)) {
		data->InsertChars(data->CursorPos, "+r+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_G, false)) {
		data->InsertChars(data->CursorPos, "+g+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_B, false)) {
		data->InsertChars(data->CursorPos, "+b+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_P, false)) {
		data->InsertChars(data->CursorPos, "+p+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_N, false)) {
		data->InsertChars(data->CursorPos, "\n");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_L, false)) {
		data->InsertChars(data->CursorPos, "+l+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_Y, false)) {
		data->InsertChars(data->CursorPos, "+y+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_W, false)) {
		data->InsertChars(data->CursorPos, "+w+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_S, false)) {
		data->InsertChars(data->CursorPos, "+s+");
	}
	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_H, false)) {
		data->InsertChars(data->CursorPos, "+h+");
	}

	if (Shortcut(ImGuiKeyModFlags_Ctrl, ImGuiKey_M, false)) {
		checkSnippet(data);
	}
}

int inputCallback(ImGuiInputTextCallbackData* data) {
	shortCut(data);

	return 0;
}

static bool init_ImGui = false;
HRESULT WINAPI Present(IDirect3DDevice9* pDevice, const RECT* pSourceRect, const RECT* pDestRect, HWND hdest, const RGNDATA* pDirtyRegion)
{
	static auto &hook = ImGuiHook::getInstance();
	if (!init_ImGui)
	{
		ImGui_ImplWin32_EnableDpiAwareness();
		/// Инициализируем ImGui
		ImGui::CreateContext();
		ImGui_ImplWin32_Init(**(HWND**)0xC17054);
		ImGui::GetIO().MouseDoubleClickTime = 0.8f;

		ImFontConfig font_config;
		font_config.Density = ImGui_ImplWin32_GetDpiScaleForHwnd(**reinterpret_cast<HWND**>(0xC17054));
		font_config.OversampleH = 1;
		font_config.OversampleV = 1;

		// Получаем путь до папки Fonts
		char pathFonstwindows[MAX_PATH]{};
		SHGetSpecialFolderPath(nullptr, pathFonstwindows, CSIDL_FONTS, true);
		_snprintf_s(pathFonstwindows, sizeof(pathFonstwindows) - 1, "%s\\Verdana.ttf", pathFonstwindows);
		// Грузим шрифт с кириллическими начертаниями, что вместо русского текста не было знаков вопроса
		ImGui::GetIO().Fonts->AddFontFromFileTTF(pathFonstwindows, 16.5f, NULL, ImGui::GetIO().Fonts->GetGlyphRangesCyrillic());

		
		// m_pDevice = reinterpret_cast<IDirect3DDevice9 *>(RwD3D9GetCurrentD3DDevice());
		ImGui_ImplDX9_Init(static_cast<IDirect3DDevice9*>(RwD3D9GetCurrentD3DDevice()));

		init_ImGui = true;
	}

	// Инициализируем render часть для нового кадра
	ImGui_ImplDX9_NewFrame();
	// Инициализируем OS часть для нового кадра
	ImGui_ImplWin32_NewFrame();

	const float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(**reinterpret_cast<HWND**>(0xC17054));
	ImGui::GetIO().DisplayFramebufferScale = ImVec2(dpiScale, dpiScale);
	ImGui::GetIO().DisplaySize.y /= dpiScale;
	ImGui::GetIO().DisplaySize.x /= dpiScale;

	if (ImGui::GetIO().MousePos.x != -FLT_MAX && ImGui::GetIO().MousePos.y != -FLT_MAX)
	{
		ImGui::GetIO().MousePos.x /= dpiScale;
		ImGui::GetIO().MousePos.y /= dpiScale;
	}

	// Создаем новый кадр внутри ImGui
	ImGui::NewFrame();

	if (ImGui::IsKeyReleased(ImGuiKey_Insert)) {
		ImGuiHook::getInstance().setRender(!ImGuiHook::getInstance().isRender());


		if (ImGuiHook::getInstance().isRender()) {
			char* adress = *reinterpret_cast<char**>(CTheScripts::ScriptSpace + 9889 * 4);
			if (adress != nullptr) {
				std::string str = adress;
				GXTEncode(str, false);
				std::string str2 = ReplaceAll(str, "+n+", "\n");
				strlcpy(buffer, cp1251_to_utf8(str2.c_str()).c_str(), 200);
			}
		}

	}

	auto windowsSize = ImGui::GetIO().DisplaySize;

	ImGui::SetNextWindowPos(ImVec2(windowsSize.x, 0.0f), ImGuiCond_FirstUseEver, ImVec2(1.0f, 0.0f));
	if (ImGuiHook::getInstance().isRender() && *reinterpret_cast<unsigned char*>(0xB7CB49) == FALSE) {
		if (ImGui::Begin("Text input", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
			if (ImGui::BeginTabBar("##tabs")) {
				if (ImGui::BeginTabItem("Input")) {
					ImGui::SetKeyboardFocusHere();
					ImGui::InputTextMultiline("##textInput", buffer, 200, ImVec2(450.0f, 200.0f), ImGuiInputTextFlags_CtrlEnterForNewLine + ImGuiInputTextFlags_CallbackAlways, inputCallback);

					if (ImGui::IsKeyReleased(ImGuiKey_Tab)) {
						setData();
					}
					ImGui::EndTabItem();
				}
				if (ImGui::BeginTabItem("Snippets")) {
					if (ImGui::Button("New")) {
						hook.getSnippets().emplace_back(new char[16]{}, new char[64]{});
					}
					if (ImGui::BeginChild("##snippetsInputs", ImVec2(450.0f, 200.0f))) {
						for (int i = 0; i < hook.getSnippets().size(); ++i) {
							ImGui::PushID(i);
							ImGui::SetNextItemWidth(150.0f);
							ImGui::InputTextWithHint("##snippetName", "snippet", hook.getSnippets().at(i).first.get(), 16);
							ImGui::SameLine();
							ImGui::SetNextItemWidth(250.0f);
							ImGui::InputTextWithHint("##snippetText", "text", hook.getSnippets().at(i).second.get(), 64, ImGuiInputTextFlags_CallbackAlways, inputCallback);
							ImGui::SameLine();
							if (ImGui::Button("X")) {
								hook.getSnippets().erase(hook.getSnippets().begin() + i);
								i = hook.getSnippets().size();
							}
							ImGui::PopID();
						}
						ImGui::EndChild();
					}
					ImGui::EndTabItem();
				}
				ImGui::EndTabBar();
			}


			if (ImGui::IsKeyReleased(ImGuiKey_Enter)) {
				ImGuiHook::getInstance().setRender(false);
			}

		}
		ImGui::End();
	}

	// Завершаем кадр ImGui
	ImGui::EndFrame();
	// Рендерим ImGuiв внутренний буффер
	ImGui::Render();
	// Отдаем Directx внутренний буффер на рендер
	ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

	return oPresent(pDevice, pSourceRect, pDestRect, hdest, pDirtyRegion);
}

HRESULT WINAPI Reset(IDirect3DDevice9* m_pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	ImGui_ImplDX9_InvalidateDeviceObjects();
	return oReset(m_pDevice, pPresentationParameters);
}

// Обработчик событий окна
LRESULT CALLBACK WindowProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	static auto& hook = ImGuiHook::getInstance();
	if (init_ImGui && *reinterpret_cast<unsigned char*>(0xB7CB49) == FALSE) {
		const LRESULT lresult = ImGui_ImplWin32_WndProcHandler(hWnd, uMsg, wParam, lParam);
		//if (hook.isRender() && !(wParam == VK_ESCAPE))
		//	return lresult;
	}

	// вызываем оригинал
	return CallWindowProcA((WNDPROC)m_pWindowProc, hWnd, uMsg, wParam, lParam);
}

void hook() {
	void** vTableDevice = *(void***)(*(DWORD*)0xC97C28);
	vmtHooks = new VMTHookManager(vTableDevice);
	oPresent = (_Present)vmtHooks->Hook(17, (void*)Present);
	oReset = (_Reset)vmtHooks->Hook(16, (void*)Reset);
}

void ImGuiHook::Init()
{
	hook();
	m_pWindowProc = SetWindowLongW(**(HWND**)0xC17054, GWL_WNDPROC, (LONG)WindowProc);
}

void ImGuiHook::Destroy()
{
	init_ImGui = false;
	vmtHooks->UnhookAll();
	ImGui_ImplDX9_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

std::vector<std::pair<std::unique_ptr<char[]>, std::unique_ptr<char[]>>>& ImGuiHook::getSnippets() {
	return snippets_;
}

bool& ImGuiHook::isRender() {
	return render;
}

void ImGuiHook::setRender(bool render) {
	show_cursor(render);
	this->render = render;
}
