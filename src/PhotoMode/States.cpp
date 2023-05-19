#include "States.h"

namespace PhotoMode
{
	void State::Camera::get()
	{
		fov = RE::PlayerCamera::GetSingleton()->worldFOV;
		translateSpeed = Cache::translateSpeedValue;
	}
	void State::Camera::set()
	{
		RE::PlayerCamera::GetSingleton()->worldFOV = fov;
		Cache::translateSpeedValue = translateSpeed;
		viewRoll = 0.0f;
	}

    void State::Time::get()
	{
		freezeTime = RE::Main::GetSingleton()->freezeTime;
		globalTimeMult = RE::BSTimer::GetCurrentGlobalTimeMult();
		timescale = RE::TESForm::LookupByEditorID<RE::TESGlobal>("timescale")->value;
	}
	void State::Time::set() const
	{
		RE::Main::GetSingleton()->freezeTime = freezeTime;
		RE::BSTimer::GetCurrentGlobalTimeMult() = globalTimeMult;
		RE::TESForm::LookupByEditorID<RE::TESGlobal>("timescale")->value = timescale;
	}

	void State::Player::get()
	{
		const auto playerRef = RE::PlayerCharacter::GetSingleton();

		visible = playerRef->Get3D() ? !playerRef->Get3D()->GetAppCulled() : true;

		rotZ = playerRef->GetAngleZ();
		pos = playerRef->GetPosition();
	}
	void State::Player::set() const
	{
		const auto playerRef = RE::PlayerCharacter::GetSingleton();

		//culling is handled by manager

		playerRef->SetRotationZ(rotZ);
		playerRef->SetPosition(pos, true);
		playerRef->UpdateActor3DPosition();
	}

	void State::GetState()
	{
		camera.get();
		time.get();
		player.get();
	}
	void State::SetState()
	{
		camera.set();
		time.set();
		player.set();
	}
}
