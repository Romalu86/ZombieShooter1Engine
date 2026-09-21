#include "zs1/zScriptExec.h"
#include "zs1/zScriptExec_engine.h"
#include "zs1/zUserMngr.h"
#include "zs1/zHighScores.h"
#include "zs1/zHelpParser.h"
#include "zs1/zDebugLog.h"
#include "zs1/zCommon.h"
#include "constant.h"
#include "sprite.h"
#include <cmath>
#include <cstdint>
namespace zs1 {
const char* ScriptExecDispatch(int command,int arg1,int arg2,const char* str1,const char* str2,int* intResult,void** objectResult)
{
    *intResult=0;
    if(command==0x01){*intResult=(int)std::sqrt((double)arg1);return nullptr;}
    if(command==0x02){script_engine::SetFloatPair(arg1,arg2);return nullptr;}
    if(command==0x03){script_engine::ToggleFlag();return nullptr;}
    if(command==0x04){script_engine::SetAngleState((float)arg1,(float)arg2);return nullptr;}
    if(command==0x05){*intResult=g_UserMngr->GetUsersCnt();return nullptr;}
    if(command==0x06)return g_UserMngr->GetUserNameByOrdinal(arg1);
    if(command==0x07){*intResult=g_UserMngr->AddUser(str1)?1:0;return nullptr;}
    if(command==0x08){if(arg1==2)g_UserMngr->SetAuxInt(str1,arg2);else g_UserMngr->SetInt(arg1!=0,str1,arg2);return nullptr;}
    if(command==0x09){*intResult=arg1==2?g_UserMngr->GetAuxInt(str1,arg2):g_UserMngr->GetInt(arg1!=0,str1,arg2);return nullptr;}
    if(command==0x0A){if(arg1==2)g_UserMngr->SetAuxStr(str1,str2);else g_UserMngr->SetStr(arg1!=0,str1,str2);return nullptr;}
    if(command==0x0B)return arg1==2?g_UserMngr->GetAuxStr(str1,str2):g_UserMngr->GetStr(arg1!=0,str1,str2);
    if(command==0x0C){(void)g_UserMngr->DeleteUserByOrdinal(arg1);return nullptr;}
    if(command==0x0D){(void)g_UserMngr->RenameUserByOrdinal(arg1,str1);return nullptr;}
    if(command==0x0E){(void)g_UserMngr->SetCurUserByOrdinal(arg1);return nullptr;}
    if(command==0x0F){*intResult=g_UserMngr->GetCurUserOrdinal();return nullptr;}
    if(command==0x10){if(g_DebugLog)g_DebugLog->Write(str1);return nullptr;}
    if(command==0x11){*intResult=g_HighScores->Add(str1,arg1,arg2);return nullptr;}
    if(command==0x12){*intResult=g_HighScores->GetCount();return nullptr;}
    if(command==0x13)return g_HighScores->GetName(arg1);
    if(command==0x14){*intResult=g_HighScores->GetValue1(arg1);return nullptr;}
    if(command==0x15){*intResult=g_HighScores->GetValue2(arg1);return nullptr;}
    if(command==0x16){g_UserMngr->Save();return nullptr;}
    if(command==0x17){*intResult=g_HelpParser->LoadLevel(arg1);return nullptr;}
    if(command==0x18){*intResult=g_HelpParser->GetItemType(arg1,arg2);return nullptr;}
    if(command==0x19)return g_HelpParser->GetItemText(arg1,arg2)->m_str;
    if(command==0x1A){*intResult=g_HelpParser->GetItemValue1(arg1,arg2);return nullptr;}
    if(command==0x1B){*intResult=g_HelpParser->GetItemValue2(arg1,arg2);return nullptr;}
    if(command==0x1C){*intResult=g_HelpParser->GetItemMetric(arg1,arg2);return nullptr;}
    if(command==0x1D){ if(arg1<0)return nullptr; unsigned char* base=(unsigned char*)script_engine::g_mainObject; int count=*reinterpret_cast<int*>(base+0xB0C); if(arg1>=count)return nullptr; void* object=reinterpret_cast<void**>(base+0xB10)[arg1]; if(!object)return nullptr; int divisor=*reinterpret_cast<int*>((unsigned char*)object+0x78); if(divisor){const std::uint32_t shifted=static_cast<std::uint32_t>(arg2)<<8;const std::int32_t numerator=static_cast<std::int32_t>(shifted);*intResult=(numerator/divisor)&0xFF;} return nullptr; }
    if(command==0x1E){*objectResult=*reinterpret_cast<void**>((unsigned char*)script_engine::g_mainObject+0xADC);return nullptr;}
    if(command==0x1F){*intResult=static_cast<int>(as1::g_baseConstants->raw[10]);return nullptr;}
    if(command==0x20){g_HighScores->ResetRecordsFile(true);return nullptr;}
    if(command==0x21){(void)g_UserMngr->SelectOrAddUserByName(str1);return nullptr;}
    if(command==0x22){ g_DebugLog=reinterpret_cast<zDebugLog*>((std::uintptr_t)0x13); g_DebugLog->Write("aassaass - bb"); return nullptr; }
    if(command==0x23){if(arg1==1)*intResult=script_engine::g_moveQueryA;else if(arg1==2)*intResult=script_engine::g_moveQueryB;else if(arg1==3)*intResult=script_engine::g_moveQueryCount;else if(arg1==4)*intResult=script_engine::g_moveQueryC;return nullptr;}
    if(command==0x24){script_engine::g_animateVehicle=(unsigned char)(arg1!=0);return nullptr;}
    if(command==0x25){as1::SPRITE* s=reinterpret_cast<as1::SPRITE*>((std::uintptr_t)(std::uint32_t)arg1);s->InsertPauseBeforeMoveActions(arg2);return nullptr;}
    if(command==0x26){script_engine::g_energyShield=(unsigned char)(arg1!=0);return nullptr;}
    if(command==0x27){*intResult=script_engine::g_energyShield?1:0;return nullptr;}
    if(command>=0x28&&command<=0x2B){as1::SPRITE* s=reinterpret_cast<as1::SPRITE*>((std::uintptr_t)(std::uint32_t)arg1);if(!s)return nullptr; if(command==0x28){float v=arg2==1?s->X():(arg2==2?s->Y():(arg2==3?s->Z():0.0f));*intResult=(int)(v*1000.0f);} else {float v=(float)arg2*0.0010000000474974513f;if(command==0x29)s->ChangeCoor(v,s->Y(),s->Z());else if(command==0x2A)s->ChangeCoor(s->X(),v,s->Z());else s->ChangeCoor(s->X(),s->Y(),v);} return nullptr;}
    if(command==0x2C){*intResult=script_engine::g_bonusBallToChange;script_engine::g_bonusBallToChange=0;return nullptr;}
    if(command==0x2D){as1::SPRITE* s=reinterpret_cast<as1::SPRITE*>((std::uintptr_t)(std::uint32_t)arg1);s->GotoNearestMoveAction();return nullptr;}
    if(command==0x2E){as1::SPRITE* s=reinterpret_cast<as1::SPRITE*>((std::uintptr_t)(std::uint32_t)arg1);if(arg2>=50)Assert(5,"var2 < 50","zScriptExec.cpp",184);script_engine::g_moveQueryA=script_engine::g_moveQueryB=script_engine::g_moveQueryC=-1;script_engine::g_moveQueryCount=0;if(!s)return nullptr;int guard=100;for(int i=(int)s->commandRecordCount()-1;i>=0;--i){if(--guard<=0){Assert(5,"0","zScriptExec.cpp",201);return nullptr;}if(s->commandRecordWord((size_t)i,0)!=0x21)continue;if(script_engine::g_moveQueryCount==arg2){script_engine::g_moveQueryA=(int)s->commandRecordWord((size_t)i,1);script_engine::g_moveQueryB=(int)s->commandRecordWord((size_t)i,2);script_engine::g_moveQueryC=(int)s->commandRecordWord((size_t)i,3);}++script_engine::g_moveQueryCount;}return nullptr;}
    if(command==0x2F){g_UserMngr->LoadAux(arg1);return nullptr;} if(command==0x30){g_UserMngr->SaveAux(arg1);return nullptr;} if(command==0x31){g_UserMngr->ClearAux();return nullptr;}


    Assert(5,"0","zScriptExec.cpp",304);
    return nullptr;
}
}
