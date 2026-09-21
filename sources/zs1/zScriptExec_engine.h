#pragma once
namespace zs1 { namespace script_engine {
extern unsigned char g_toggleFlag;
extern float g_pairX;
extern float g_pairY;
extern float g_angleRadians;
extern float g_angleSecond;
extern int g_moveQueryA,g_moveQueryB,g_moveQueryC,g_moveQueryCount;
extern unsigned char g_animateVehicle;
extern unsigned char g_energyShield;
extern int g_bonusBallToChange;


unsigned char AnimateVehicleEnabled() noexcept;

unsigned char HasEnergyShield() noexcept;

void ClearEnergyShield() noexcept;

void SetBonusBallToChange(int spriteHandle) noexcept;
extern void* g_mainObject;

void ToggleFlag() noexcept;
unsigned char GetToggleFlag() noexcept;
void SetFloatPair(int a, int b) noexcept;
void SetAngleState(float degrees, float second) noexcept;
} }
