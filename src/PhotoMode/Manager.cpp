#include "Manager.h"

#include "ImGui/Util.h"
#include "Overrides.h"
#include "Screenshots.h"
#include "Settings.h"

namespace PhotoMode
{
	bool Manager::GetValid()
	{
		static constexpr std::array badMenus{
			RE::MainMenu::MENU_NAME,
			RE::MistMenu::MENU_NAME,
			RE::JournalMenu::MENU_NAME,
			RE::InventoryMenu::MENU_NAME,
			RE::MagicMenu::MENU_NAME,
			RE::MapMenu::MENU_NAME,
			RE::BookMenu::MENU_NAME,
			RE::LockpickingMenu::MENU_NAME,
			RE::StatsMenu::MENU_NAME,
			RE::ContainerMenu::MENU_NAME,
			RE::DialogueMenu::MENU_NAME,
			RE::TweenMenu::MENU_NAME,
			RE::SleepWaitMenu::MENU_NAME,
			RE::RaceSexMenu::MENU_NAME,
			"CustomMenu"sv
		};

		if (const auto UI = RE::UI::GetSingleton(); !UI || std::ranges::any_of(badMenus, [&](const auto& menuName) { return UI->IsMenuOpen(menuName); })) {
			return false;
		}

		if (const auto player = RE::PlayerCharacter::GetSingleton(); !player || !player->Get3D() && !player->Get3D(true)) {
			return false;
		}

		return true;
	}

	void Manager::LoadSettings(CSimpleIniA& a_ini)
	{
		ini::get_value(a_ini, hotKey, "PhotoMode", "Hotkey", ";Toggle photomode. Default is N\n;DXScanCodes : https://www.creationkit.com/index.php?title=Input_Script");
		ini::get_value(a_ini, resetAllHoldDuration, "PhotoMode", "ResetAllHoldDuration", "How long should Reset key be held down (in seconds).");
	}

	std::uint32_t Manager::GetHotKey() const
	{
		return hotKey;
	}

	float Manager::GetResetHoldDuration() const
	{
		return resetAllHoldDuration;
	}

	bool Manager::GetResetAll() const
	{
		return resetAll;
	}

	void Manager::DoResetAll(bool a_enable)
	{
		resetAll = a_enable;
	}

	bool Manager::IsActive() const
	{
		return activated;
	}

	void Manager::GetOriginalState()
	{
		originalState.GetState();

		Override::InitOverrides();

		// this isn't updated per frame
		currentState.player.visible = originalState.player.visible;
		if (RE::PlayerCamera::GetSingleton()->IsInFirstPerson()) {
			originalCameraState = RE::CameraState::kFirstPerson;
		} else {  // assume
			originalCameraState = RE::CameraState::kThirdPerson;
		}

		menusAlreadyHidden = !RE::UI::GetSingleton()->IsShowingMenus();

		// init imagespace
		const auto IMGS = RE::ImageSpaceManager::GetSingleton();
		if (IMGS->currentBaseData) {
			imageSpaceData = *IMGS->currentBaseData;
		}
		imageSpaceData.tint.amount = 0.0f;
		imageSpaceData.tint.color = { 1.0f, 1.0f, 1.0f };
		IMGS->overrideBaseData = &imageSpaceData;

		//disable saving
		RE::PlayerCharacter::GetSingleton()->byCharGenFlag.set(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);
	}

	void Manager::Revert(bool a_deactivate)
	{
		if (a_deactivate) {
			RevertTab(-1);
			// reset UI
			if (!menusAlreadyHidden && !RE::UI::GetSingleton()->IsShowingMenus()) {
				RE::UI::GetSingleton()->ShowMenus(true);
			}
			tabIndex = 0;
			doResetWindow = true;
		} else {
			RevertTab(resetAll ? -1 : tabIndex);
			RE::DebugNotification(fmt::format("Photomode : {} settings reset", resetAll ? "All" : tabEnumLC[tabIndex]).c_str());
			if (resetAll) {
				DoResetAll(false);
			}
		}
	}

	void Manager::RevertTab(std::int32_t a_tabIndex)
	{
		const bool resetAllTabs = a_tabIndex == -1;

		// Camera
		if (resetAllTabs || a_tabIndex == 0) {
			originalState.camera.set();
			// revert grid
			Grid::gridType = Grid::kDisabled;
			// revert DOF
			if (const auto& effect = RE::ImageSpaceManager::GetSingleton()->effects[RE::ImageSpaceManager::ImageSpaceEffectEnum::DepthOfField]) {
				static_cast<RE::ImageSpaceEffectDepthOfField*>(effect)->enabled = true;
			}
			// revert view roll
			currentState.camera.viewRoll = 0.0f;
		}
		// Time/Weather
		if (resetAllTabs || a_tabIndex == 1) {
			originalState.time.set();
			timescaleMult = 1.0f;
			// revert weather
			if (weatherForced) {
				Override::weathers.Revert();
				weatherForced = false;
			}
		}
		// Player
		if (resetAllTabs || a_tabIndex == 2) {
			originalState.player.set();
			// revert current values
			currentState.player.pos = RE::NiPoint3();
			if (!currentState.player.visible) {
				if (const auto root = RE::PlayerCharacter::GetSingleton()->Get3D()) {
					root->CullGeometry(false);
				}
				currentState.player.visible = true;
			}
			// reset expressions
			MFG::RevertAllModifiers();
			// revert effects
			if (effectsPlayed) {
				Override::effectShaders.Revert();
				effectsPlayed = false;
			}
			if (vfxPlayed) {
				Override::effectVFX.Revert();
				vfxPlayed = false;
			}
		}
		// Filters
		if (resetAllTabs || a_tabIndex == 3) {
			// reset imagespace
			const auto IMGS = RE::ImageSpaceManager::GetSingleton();
			if (resetAllTabs) {
				IMGS->overrideBaseData = nullptr;
			} else if (IMGS->overrideBaseData) {
				if (IMGS->currentBaseData) {
					imageSpaceData = *IMGS->currentBaseData;
				}
				imageSpaceData.tint.amount = 0.0f;
				imageSpaceData.tint.color = { 1.0f, 1.0f, 1.0f };
				IMGS->overrideBaseData = &imageSpaceData;
			}
			// reset imod
			if (imodPlayed) {
				Override::imods.Revert();
				imodPlayed = false;
			}
		}
		// Screenshots
		if (a_tabIndex == 4) {
			Screenshot::Manager::GetSingleton()->Revert();
		}
	}

	void Manager::Activate()
	{
		GetOriginalState();

		RE::PlayerCamera::GetSingleton()->ToggleFreeCameraMode(false);
		RE::ControlMap::GetSingleton()->ToggleControls(controlFlags, false);

		activated = true;
	}

	void Manager::Deactivate()
	{
		Revert(true);

		Settings::GetSingleton()->SaveSettings();

		if (const auto camera = RE::PlayerCamera::GetSingleton()) {
			switch (originalCameraState) {
			case RE::CameraState::kFirstPerson:
				camera->ForceFirstPerson();
				break;
			default:
				camera->ForceThirdPerson();
				break;
			}
		}

		// reset controls
		const auto controlMap = RE::ControlMap::GetSingleton();
		controlMap->ToggleControls(controlFlags, true);
		controlMap->ignoreKeyboardMouse = false;

		// allow saving
		RE::PlayerCharacter::GetSingleton()->byCharGenFlag.reset(RE::PlayerCharacter::ByCharGenFlag::kDisableSaving);

		activated = false;
	}

	// called by Input
	void Manager::ToggleActive()
	{
		if (!activated) {
			if (GetValid()) {
				Activate();
			}
		} else {
			if (ImGui::GetIO().WantTextInput) {
				return;
			}
			Deactivate();
		}
	}

	float Manager::GetViewRoll(const float a_fallback) const
	{
		return IsActive() ? currentState.camera.viewRoll : a_fallback;
	}

	void Manager::Draw()
	{
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->Pos);
		ImGui::SetNextWindowSize(viewport->Size);

		ImGui::Begin("##Main", nullptr, ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBringToFrontOnFocus);

		DrawControls();
		DrawBar();
		Grid::Draw();

		ImGui::End();
	}

	void Manager::OnFrameUpdate() const
	{
		RE::ControlMap::GetSingleton()->ignoreKeyboardMouse = ImGui::GetIO().WantTextInput;
		if (weatherForced) {
			RE::Sky::GetSingleton()->lastWeatherUpdate = RE::Calendar::GetSingleton()->gameHour->value;
		}
	}

	void Manager::DrawControls()
	{
		const auto viewport = ImGui::GetMainViewport();

		const static auto center = viewport->GetCenter();
		const static auto third_width = viewport->Size.x / 3;
		const static auto third_height = viewport->Size.y / 3;

		ImGui::SetNextWindowPos(ImVec2(center.x + third_width, center.y + third_height), ImGuiCond_Always, ImVec2(0.5, 0.5));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x / 3.5f, viewport->Size.y / 3.5f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.66f);

		ImGui::Begin("PhotoMode", nullptr, ImGuiWindowFlags_NoMouseInputs);

		if (ImGui::BeginTabBar("PhotoMode#TopBar", ImGuiTabBarFlags_FittingPolicyScroll)) {
			if (ImGui::OpenTabOnHover("Camera", doResetWindow ? ImGuiTabItemFlags_SetSelected : 0)) {
				tabIndex = 0;

				if (doResetWindow) {
					doResetWindow = false;
				}

				ImGui::EnumSlider("Grid", &Grid::gridType, Grid::gridEnum);

				ImGui::Slider("Field of View", &RE::PlayerCamera::GetSingleton()->worldFOV, 20.0f, 120.0f);
				ImGui::Slider("View Roll", &currentState.camera.viewRoll, -2.0f, 2.0f);
				ImGui::Slider("Translate Speed",
					&Cache::translateSpeedValue,  // fFreeCameraTranslationSpeed:Camera
					0.1f, 50.0f);

				if (const auto& effect = RE::ImageSpaceManager::GetSingleton()->effects[RE::ImageSpaceManager::ImageSpaceEffectEnum::DepthOfField]) {
					if (ImGui::TreeNode("Depth of Field")) {
						const auto dofEffect = static_cast<RE::ImageSpaceEffectDepthOfField*>(effect);

						ImGui::OnOffToggle("Enabled", &dofEffect->enabled);

						ImGui::BeginDisabled(!dofEffect->enabled);
						ImGui::Slider("Strength", &Cache::DOF::blurMultiplier, 0.0f, 1.0f);
						ImGui::Slider("Near Distance", &Cache::DOF::nearDist, 0.0f, 1000.0f);
						ImGui::Slider("Near Range", &Cache::DOF::nearRange, 0.0f, 1000.0f);
						ImGui::Slider("Far Distance", &Cache::DOF::farDist, 0.0f, 100000.0f);
						ImGui::Slider("Far Range", &Cache::DOF::farRange, 0.0f, 100000.0f);
						ImGui::EndDisabled();

						ImGui::TreePop();
					}
				}

				ImGui::EndTabItem();
			}

			if (ImGui::OpenTabOnHover("Time/Weather")) {
				tabIndex = 1;

				ImGui::OnOffToggle("Freeze Time", &RE::Main::GetSingleton()->freezeTime);
				ImGui::Slider("Global Time Mult", &RE::BSTimer::GetCurrentGlobalTimeMult(), 0.0f, 2.0f);

				ImGui::Dummy({ 0, 5 });

				auto& gameHour = RE::Calendar::GetSingleton()->gameHour->value;
				ImGui::Slider("Game Hour", &gameHour, 0.0f, 23.99f, std::format("{:%I:%M %p}", std::chrono::duration<float, std::ratio<3600>>(gameHour)).c_str());

				if (ImGui::DragOnHover("Timescale Mult", &timescaleMult, 10, 1.0f, 1000.0f, "%.fX")) {
					static auto timescale = RE::Calendar::GetSingleton()->timeScale;
					timescale->value = originalState.time.timescale * timescaleMult;
				}

				ImGui::Dummy({ 0, 15 });

				if (const auto weather = Override::weathers.GetFormResultFromCombo()) {
					weatherForced = true;
					Override::weathers.Apply(weather);
				}

				ImGui::EndTabItem();
			}

			if (ImGui::OpenTabOnHover("Player")) {
				tabIndex = 2;

				static auto player = RE::PlayerCharacter::GetSingleton();
				auto&       playerState = currentState.player;

				if (ImGui::OnOffToggle("Show player", &playerState.visible)) {
					player->Get3D()->CullGeometry(!playerState.visible);
				}

				ImGui::BeginDisabled(!playerState.visible);
				if (ImGui::BeginTabBar("Player#TopBar", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
					// ugly af, improve later
					if (ImGui::OpenTabOnHover("Expressions")) {
						using namespace MFG;

						if (ImGui::TreeNode("Expressions##node")) {
							if (ImGui::EnumSlider("Expression", &expressionData.modifier, expressions)) {
								expressionData.ApplyExpression(player);
							}
							ImGui::TreePop();
						}

						if (ImGui::TreeNode("Phoneme")) {
							for (std::uint32_t i = 0; i < phonemes.size(); i++) {
								if (ImGui::DragOnHover(phonemes[i], &phonemeData[i].strength)) {
									phonemeData[i].ApplyPhenome(i, player);
								}
							}
							ImGui::TreePop();
						}

						if (ImGui::TreeNode("Modifier")) {
							for (std::uint32_t i = 0; i < modifiers.size(); i++) {
								if (ImGui::DragOnHover(modifiers[i], &modifierData[i].strength)) {
									modifierData[i].ApplyModifier(i, player);
								}
							}
							ImGui::TreePop();
						}
						ImGui::EndTabItem();
					}

					if (ImGui::OpenTabOnHover("Poses")) {
						if (const auto idle = Override::idles.GetFormResultFromCombo()) {
							if (idlePlayed) {
								Override::idles.Revert(false);
							}
							Override::idles.Apply(idle);
							idlePlayed = true;
						}
						ImGui::EndTabItem();
					}

					if (ImGui::OpenTabOnHover("Effects")) {
						if (const auto effectShader = Override::effectShaders.GetFormResultFromCombo()) {
							effectsPlayed = true;
							Override::effectShaders.Apply(effectShader);
						}
						if (const auto vfx = Override::effectVFX.GetFormResultFromCombo()) {
							vfxPlayed = true;
							Override::effectVFX.Apply(vfx);
						}
						ImGui::EndTabItem();
					}

					if (ImGui::OpenTabOnHover("Transforms")) {
						playerState.rotZ = RE::rad_to_deg(player->GetAngleZ());
						if (ImGui::Slider("Rotation", &playerState.rotZ, 0.0f, 360.0f)) {
							player->SetRotationZ(RE::deg_to_rad(playerState.rotZ));
						}

						bool update = ImGui::Slider("Position Left/Right", &playerState.pos.x, -100.0f, 100.0f);
						update |= ImGui::Slider("Position Near/Far", &playerState.pos.y, -100.0f, 100.0f);
						// update |= ImGui::Slider("Elevation", &playerState.pos.z, -100.0f, 100.0f);

						if (update) {
							player->SetPosition({ originalState.player.pos + playerState.pos }, true);
						}

						ImGui::EndTabItem();
					}
					ImGui::EndTabBar();
				}
				ImGui::EndDisabled();
				ImGui::EndTabItem();
			}

			if (ImGui::OpenTabOnHover("Filter/Lighting")) {
				tabIndex = 3;

				if (const auto imageSpace = Override::imods.GetFormResultFromCombo()) {
					imodPlayed = true;
					Override::imods.Apply(imageSpace);
				}

				ImGui::Dummy({ 0, 15 });

				if (const auto& overrideData = RE::ImageSpaceManager::GetSingleton()->overrideBaseData) {
					ImGui::Slider("Brightness", &overrideData->cinematic.brightness, 0.0f, 3.0f);
					ImGui::Slider("Saturation", &overrideData->cinematic.saturation, 0.0f, 3.0f);
					ImGui::Slider("Contrast", &overrideData->cinematic.contrast, 0.0f, 3.0f);

					if (ImGui::TreeNode("Tint")) {
						ImGui::Slider("Tint Alpha", &overrideData->tint.amount, 0.0f, 1.0f);
						ImGui::Slider("Red", &overrideData->tint.color.red, 0.0f, 1.0f);
						ImGui::Slider("Blue", &overrideData->tint.color.blue, 0.0f, 1.0f);
						ImGui::Slider("Green", &overrideData->tint.color.green, 0.0f, 1.0f);

						ImGui::TreePop();
					}
				} else {
					RE::ImageSpaceManager::GetSingleton()->overrideBaseData = &imageSpaceData;
				}

				ImGui::EndTabItem();
			}

			if (ImGui::OpenTabOnHover("Screenshots")) {
				tabIndex = 4;

				Screenshot::Manager::GetSingleton()->Draw();
				ImGui::EndTabItem();
			}

			ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
			ImGui::TabItemButton("<", ImGuiTabItemFlags_Leading);
			ImGui::TabItemButton(">", ImGuiTabItemFlags_Trailing);
			ImGui::PopItemFlag();

			ImGui::EndTabBar();
		}

		ImGui::End();
	}

	void Manager::DrawBar() const
	{
		const auto viewport = ImGui::GetMainViewport();

		const static auto center = viewport->GetCenter();
		const static auto offset = viewport->Size.y / 20;

		ImGui::SetNextWindowPos(ImVec2(center.x, (center.y + viewport->Size.y / 2) - offset), ImGuiCond_Always, ImVec2(0.5, 0.25));
		ImGui::SetNextWindowBgAlpha(0.66f);

		ImGui::BeginChild("##Tab", ImVec2(viewport->Size.x / 3.25f, offset * 0.8f), false, ImGuiWindowFlags_AlwaysAutoResize);
		{
			const auto str = fmt::format("PRNTSCRN) TAKE SNAPSHOT T) TOGGLE MENU R) RESET {} N) EXIT", GetResetAll() ? "ALL" : tabEnum[tabIndex]);
			ImGui::CenterLabel(str.c_str());
		}
		ImGui::EndChild();
	}

	struct FromEulerAnglesZXY
	{
		static void thunk(RE::NiMatrix3* a_matrix, float a_z, float a_x, float a_y)
		{
			return func(a_matrix, a_z, a_x, get<Manager>()->GetViewRoll(a_y));
		}
		static inline REL::Relocation<decltype(thunk)> func;
	};

	void InstallHooks()
	{
		REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(49814, 50744), 0x1B };  // FreeCamera::GetRotation
		stl::write_thunk_call<FromEulerAnglesZXY>(target.address());

		Override::InstallHooks();
	}
}
