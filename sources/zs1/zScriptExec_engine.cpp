#include "zs1/zScriptExec_engine.h"
namespace zs1 { namespace script_engine {
unsigned char g_toggleFlag = 0;
float g_pairX = 1000.0f;
float g_pairY = 1000.0f;
float g_angleRadians = 0.0f;
float g_angleSecond = 0.0f;
int g_moveQueryA=0,g_moveQueryB=0,g_moveQueryC=0,g_moveQueryCount=0;
unsigned char g_animateVehicle=1;
unsigned char g_energyShield=0;
int g_bonusBallToChange=0;
void* g_mainObject=nullptr;


unsigned char AnimateVehicleEnabled() noexcept { return g_animateVehicle; }


unsigned char HasEnergyShield() noexcept { return g_energyShield; }


void ClearEnergyShield() noexcept { g_energyShield = 0; }


void SetBonusBallToChange(int spriteHandle) noexcept { g_bonusBallToChange = spriteHandle; }


void ToggleFlag() noexcept
{
    g_toggleFlag = static_cast<unsigned char>(!g_toggleFlag);
}


unsigned char GetToggleFlag() noexcept
{
    return g_toggleFlag;
}


void SetFloatPair(int a, int b) noexcept
{
    g_pairX = static_cast<float>(a);
    g_pairY = static_cast<float>(b);
}


void SetAngleState(float degrees, float second) noexcept
{
    g_angleRadians = degrees * 3.1415927410125732421875f * 0.00555555569007992744446f;
    g_angleSecond = second;
}
} }
