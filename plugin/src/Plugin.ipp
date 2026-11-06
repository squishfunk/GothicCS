// This file is included separately for each engine version

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cmath>
#include <cstdarg>
#include <vector>
#include <algorithm>
#include <string>
#include <map>
#include "cs/Movement.h"
#include "cs/ViewModelData.h"
#include "cs/Player.h"

namespace GOTHIC_NAMESPACE
{
#include "Sprites.inl"
#include "ViewModel.inl"
#include "Hits.inl"
#include "GothicCS.inl"
#include "Effects.inl"
#include "Combat.inl"
#include "Grenades.inl"
#include "Bomb.inl"
#include "Bots.inl"
#include "Round.inl"
#include "Hud.inl"
#include "HudSprites.inl"
#include "Main.inl"

	// The CS viewmodel, effects and HUD polygons are drawn when the screen
	// view draws its items (Gothic draws its own 2D polygons in this phase).
	// Damage on the hero in CS mode becomes CS damage (see GothicCS::PlayerDamageBegin).
	void __fastcall oCNpc_OnDamage(oCNpc* self, void* vtable, oCNpc::oSDamageDescriptor& desc);
	auto Hook_oCNpc_OnDamage = Union::CreateHook(SIGNATURE_OF(static_cast<void (oCNpc::*)(oCNpc::oSDamageDescriptor&)>(&oCNpc::OnDamage)),
		&oCNpc_OnDamage, Union::HookType::Hook_Detours);
	void __fastcall oCNpc_OnDamage(oCNpc* self, void* vtable, oCNpc::oSDamageDescriptor& desc)
	{
		int savedHp = 0;
		const bool cs = GothicCS::PlayerDamageBegin(self, savedHp);
		Hook_oCNpc_OnDamage(self, vtable, desc);
		if (cs) GothicCS::PlayerDamageEnd(self, savedHp, desc);
	}

	// Gothic's spawn manager removes NPCs far from its cached camera position.
	// In CS mode the camera moves fast (noclip, bhop): refresh the cached position
	// first, and never remove CS bots (their gun vob would stay and keep shooting).
	int __fastcall oCSpawnManager_CheckRemoveNpc(oCSpawnManager* self, void* vtable, oCNpc* npc);
	auto Hook_oCSpawnManager_CheckRemoveNpc = Union::CreateHook(SIGNATURE_OF(&oCSpawnManager::CheckRemoveNpc),
		&oCSpawnManager_CheckRemoveNpc, Union::HookType::Hook_Detours);
	int __fastcall oCSpawnManager_CheckRemoveNpc(oCSpawnManager* self, void* vtable, oCNpc* npc)
	{
		if (GothicCS::S().active)
		{
			if (GothicCS::IsBotNpc(npc)) return FALSE;
			if (ogame && ogame->GetCameraVob()) self->InitCameraPos();
		}
		return Hook_oCSpawnManager_CheckRemoveNpc(self, vtable, npc);
	}

	// Gothic's keyboard input is muted in CS mode (see GothicCS::BlockGothicInput).
	int __fastcall zCInput_Win32_KeyPressed(zCInput_Win32* self, void* vtable, int key);
	auto Hook_KeyPressed = Union::CreateHook(SIGNATURE_OF(&zCInput_Win32::KeyPressed), &zCInput_Win32_KeyPressed, Union::HookType::Hook_Detours);
	int __fastcall zCInput_Win32_KeyPressed(zCInput_Win32* self, void* vtable, int key)
	{
		return GothicCS::BlockGothicKey(key) ? 0 : Hook_KeyPressed(self, vtable, key);
	}

	int __fastcall zCInput_Win32_KeyToggled(zCInput_Win32* self, void* vtable, int key);
	auto Hook_KeyToggled = Union::CreateHook(SIGNATURE_OF(&zCInput_Win32::KeyToggled), &zCInput_Win32_KeyToggled, Union::HookType::Hook_Detours);
	int __fastcall zCInput_Win32_KeyToggled(zCInput_Win32* self, void* vtable, int key)
	{
		return GothicCS::BlockGothicKey(key) ? 0 : Hook_KeyToggled(self, vtable, key);
	}

	unsigned short __fastcall zCInput_Win32_GetKey(zCInput_Win32* self, void* vtable, int a, int b);
	auto Hook_GetKey = Union::CreateHook(SIGNATURE_OF(&zCInput_Win32::GetKey), &zCInput_Win32_GetKey, Union::HookType::Hook_Detours);
	unsigned short __fastcall zCInput_Win32_GetKey(zCInput_Win32* self, void* vtable, int a, int b)
	{
		const unsigned short k = Hook_GetKey(self, vtable, a, b);
		return (k && GothicCS::BlockGothicKey(k)) ? 0 : k;   // consumed, not passed on
	}

	float __fastcall zCInput_Win32_GetState(zCInput_Win32* self, void* vtable, unsigned short logical);
	auto Hook_GetState = Union::CreateHook(SIGNATURE_OF(&zCInput_Win32::GetState), &zCInput_Win32_GetState, Union::HookType::Hook_Detours);
	float __fastcall zCInput_Win32_GetState(zCInput_Win32* self, void* vtable, unsigned short logical)
	{
		return GothicCS::BlockGothicInput() ? 0.0f : Hook_GetState(self, vtable, logical);
	}

	int __fastcall zCInput_Win32_GetToggled(zCInput_Win32* self, void* vtable, unsigned short logical);
	auto Hook_GetToggled = Union::CreateHook(SIGNATURE_OF(&zCInput_Win32::GetToggled), &zCInput_Win32_GetToggled, Union::HookType::Hook_Detours);
	int __fastcall zCInput_Win32_GetToggled(zCInput_Win32* self, void* vtable, unsigned short logical)
	{
		return GothicCS::BlockGothicInput() ? 0 : Hook_GetToggled(self, vtable, logical);
	}

	void __fastcall zCView_DrawItems(zCView* self, void* vtable);
	auto Hook_zCView_DrawItems = Union::CreateHook(SIGNATURE_OF(&zCView::DrawItems), &zCView_DrawItems, Union::HookType::Hook_Detours);
	void __fastcall zCView_DrawItems(zCView* self, void* vtable)
	{
		if (self == screen)
			GothicCS::BeforeScreenItems();
		Hook_zCView_DrawItems(self, vtable);
	}

	// NOTE! Callbacks won't be called by default, you need to uncomment
	// hooks that will call specific callback

	void Game_EntryPoint()
	{

	}

	void Game_Init()
	{

	}

	void Game_Exit()
	{

	}

	void Game_PreLoop()
	{

	}

	void Game_Loop()
	{
		GothicCS::Loop();
	}

	void Game_PostLoop()
	{

	}

	void Game_MenuLoop()
	{

	}

	void Game_SaveBegin()
	{

	}

	void Game_SaveEnd()
	{

	}

	void LoadBegin()
	{

	}

	void LoadEnd()
	{

	}

	void Game_LoadBegin_NewGame()
	{
		LoadBegin();
	}

	void Game_LoadEnd_NewGame()
	{
		LoadEnd();
	}

	void Game_LoadBegin_SaveGame()
	{
		LoadBegin();
	}

	void Game_LoadEnd_SaveGame()
	{
		LoadEnd();
	}

	void Game_LoadBegin_ChangeLevel()
	{
		LoadBegin();
	}

	void Game_LoadEnd_ChangeLevel()
	{
		LoadEnd();
	}

	void Game_LoadBegin_TriggerChangeLevel()
	{

	}

	void Game_LoadEnd_TriggerChangeLevel()
	{

	}

	void Game_Pause()
	{

	}

	void Game_Unpause()
	{

	}

	void Game_DefineExternals()
	{

	}

	void Game_ApplySettings()
	{

	}

	/*void __fastcall WinMain_EntryPoint(Union::Registers& reg);
	auto Partial_WinMain = Union::CreatePartialHook(reinterpret_cast<void*>(zSwitch(0x004F3E18, 0x00506816, 0x005000F8, 0x00502D7B)), &WinMain_EntryPoint);
	void __fastcall WinMain_EntryPoint(Union::Registers& reg)
	{
		Game_EntryPoint();
	}*/

	/*void __fastcall oCGame_Init(oCGame* self, void* vtable);
	auto Hook_oCGame_Init = Union::CreateHook(SIGNATURE_OF(&oCGame::Init), &oCGame_Init, Union::HookType::Hook_Detours);
	void __fastcall oCGame_Init(oCGame* self, void* vtable)
	{
		Hook_oCGame_Init(self, vtable);
		Game_Init();
	}*/

	/*void __fastcall CGameManager_Done(CGameManager* self, void* vtable);
	auto Hook_CGameManager_Done = Union::CreateHook(SIGNATURE_OF(&CGameManager::Done), &CGameManager_Done, Union::HookType::Hook_Detours);
	void __fastcall CGameManager_Done(CGameManager* self, void* vtable)
	{
		Game_Exit();
		Hook_CGameManager_Done(self, vtable);
	}*/

	/*void __fastcall oCGame_Render(oCGame* self, void* vtable);
	auto Hook_oCGame_Render = Union::CreateHook(SIGNATURE_OF(&oCGame::Render), &oCGame_Render, Union::HookType::Hook_Detours);
	void __fastcall oCGame_Render(oCGame* self, void* vtable)
	{
		Game_PreLoop();
		Hook_oCGame_Render(self, vtable);
		Game_PostLoop();
	}*/

	void __fastcall oCGame_MainWorld_Render(Union::Registers& reg);
	auto Partial_zCWorld_Render = Union::CreatePartialHook(reinterpret_cast<void*>(zSwitch(0x0063DC76, 0x0066498B, 0x0066BA76, 0x006C87EB)), &oCGame_MainWorld_Render);
	void __fastcall oCGame_MainWorld_Render(Union::Registers& reg)
	{
		Game_Loop();
	}

	/*void __fastcall zCMenu_Render(zCMenu* self, void* vtable);
	auto Hook_zCMenu_Render = Union::CreateHook(SIGNATURE_OF(&zCMenu::Render), &zCMenu_Render, Union::HookType::Hook_Detours);
	void __fastcall zCMenu_Render(zCMenu* self, void* vtable)
	{
		Hook_zCMenu_Render(self, vtable);
		Game_MenuLoop();
	}*/

	/*void __fastcall oCGame_WriteSaveGame(oCGame* self, void* vtable, int slot, zBOOL saveGlobals);
	auto Hook_oCGame_WriteSaveGame = Union::CreateHook(SIGNATURE_OF(&oCGame::WriteSavegame), &oCGame_WriteSaveGame, Union::HookType::Hook_Detours);
	void __fastcall oCGame_WriteSaveGame(oCGame* self, void* vtable, int slot, zBOOL saveGlobals)
	{
		Game_SaveBegin();
		Hook_oCGame_WriteSaveGame(self, vtable, slot, saveGlobals);
		Game_SaveEnd();
	}*/

	/*void __fastcall oCGame_LoadGame(oCGame* self, void* vtable, int slot, const zSTRING& levelPath);
	auto Hook_oCGame_LoadGame = Union::CreateHook(SIGNATURE_OF(&oCGame::LoadGame), &oCGame_LoadGame, Union::HookType::Hook_Detours);
	void __fastcall oCGame_LoadGame(oCGame* self, void* vtable, int slot, const zSTRING& levelPath)
	{
		Game_LoadBegin_NewGame();
		Hook_oCGame_LoadGame(self, vtable, slot, levelPath);
		Game_LoadEnd_NewGame();
	}*/

	/*void __fastcall oCGame_LoadSaveGame(oCGame* self, void* vtable, int slot, zBOOL loadGlobals);
	auto Hook_oCGame_LoadSaveGame = Union::CreateHook(SIGNATURE_OF(&oCGame::LoadSavegame), &oCGame_LoadSaveGame, Union::HookType::Hook_Detours);
	void __fastcall oCGame_LoadSaveGame(oCGame* self, void* vtable, int slot, zBOOL loadGlobals)
	{
		Game_LoadBegin_SaveGame();
		Hook_oCGame_LoadSaveGame(self, vtable, slot, loadGlobals);
		Game_LoadEnd_SaveGame();
	}*/

	/*void __fastcall oCGame_ChangeLevel(oCGame* self, void* vtable, const zSTRING& levelpath, const zSTRING& startpoint);
	auto Hook_oCGame_ChangeLevel = Union::CreateHook(SIGNATURE_OF(&oCGame::ChangeLevel), &oCGame_ChangeLevel, Union::HookType::Hook_Detours);
	void __fastcall oCGame_ChangeLevel(oCGame* self, void* vtable, const zSTRING& levelpath, const zSTRING& startpoint)
	{
		Game_LoadBegin_ChangeLevel();
		Hook_oCGame_ChangeLevel(self, vtable, levelpath, startpoint);
		Game_LoadEnd_ChangeLevel();
	}*/

	/*void __fastcall oCGame_TriggerChangeLevel(oCGame* self, void* vtable, const zSTRING& levelpath, const zSTRING& startpoint);
	auto Hook_oCGame_TriggerChangeLevel = Union::CreateHook(SIGNATURE_OF(&oCGame::TriggerChangeLevel), &oCGame_TriggerChangeLevel, Union::HookType::Hook_Detours);
	void __fastcall oCGame_TriggerChangeLevel(oCGame* self, void* vtable, const zSTRING& levelpath, const zSTRING& startpoint)
	{
		Game_LoadBegin_TriggerChangeLevel();
		Hook_oCGame_TriggerChangeLevel(self, vtable, levelpath, startpoint);
		Game_LoadEnd_TriggerChangeLevel();
	}*/

/*#if ENGINE <= Engine_G1A
	void __fastcall oCGame_Pause(oCGame* self, void* vtable);
#else
	void __fastcall oCGame_Pause(oCGame* self, void* vtable, zBOOL sessionPaused);
#endif
	auto Hook_oCGame_Pause = Union::CreateHook(SIGNATURE_OF(&oCGame::Pause), &oCGame_Pause, Union::HookType::Hook_Detours);
#if ENGINE <= Engine_G1A
	void __fastcall oCGame_Pause(oCGame* self, void* vtable)
#else
	void __fastcall oCGame_Pause(oCGame* self, void* vtable, zBOOL sessionPaused)
#endif
	{
#if ENGINE <= Engine_G1A
		Hook_oCGame_Pause(self, vtable);
#else
		Hook_oCGame_Pause(self, vtable, sessionPaused);
#endif
		Game_Pause();
	}*/

	/*void __fastcall oCGame_Unpause(oCGame* self, void* vtable);
	auto Hook_oCGame_Unpause = Union::CreateHook(SIGNATURE_OF(&oCGame::Unpause), &oCGame_Unpause, Union::HookType::Hook_Detours);
	void __fastcall oCGame_Unpause(oCGame* self, void* vtable)
	{
		Hook_oCGame_Unpause(self, vtable);
		Game_Unpause();
	}*/

	/*void __fastcall oCGame_DefineExternals_Ulfi(oCGame* self, void* vtable, zCParser* parser);
	auto Hook_oCGame_DefineExternals_Ulfi = Union::CreateHook(SIGNATURE_OF(&oCGame::DefineExternals_Ulfi), &oCGame_DefineExternals_Ulfi, Union::HookType::Hook_Detours);
	void __fastcall oCGame_DefineExternals_Ulfi(oCGame* self, void* vtable, zCParser* parser)
	{
		Hook_oCGame_DefineExternals_Ulfi(self, vtable, parser);
		Game_DefineExternals();
	}*/

	/*void __fastcall CGameManager_ApplySomeSettings(CGameManager* self, void* vtable);
	auto Hook_CGameManager_ApplySomeSettings = Union::CreateHook(SIGNATURE_OF(&CGameManager::ApplySomeSettings), &CGameManager_ApplySomeSettings, Union::HookType::Hook_Detours);
	void __fastcall CGameManager_ApplySomeSettings(CGameManager* self, void* vtable)
	{
		Hook_CGameManager_ApplySomeSettings(self, vtable);
		Game_ApplySettings();
	}*/
}
