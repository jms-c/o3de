/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */


// Description : implementation of the CXConsole class.


#include "CrySystem_precompiled.h"
#include "XConsole.h"
#include "XConsoleVariable.h"
#include "System.h"
#include "ConsoleBatchFile.h"

#include <ITimer.h>
#include <IRenderer.h>
#include <ISystem.h>
#include <ILog.h>
#include <IProcess.h>
#include <IRenderAuxGeom.h>
#include "ConsoleHelpGen.h"         // CConsoleHelpGen

#include <AzFramework/Input/Devices/Keyboard/InputDeviceKeyboard.h>
#include <AzFramework/Input/Devices/Mouse/InputDeviceMouse.h>
#include <AzCore/std/string/conversions.h>
#include <AzCore/std/algorithm.h>

#include <LyShine/Bus/UiCursorBus.h>
//#define DEFENCE_CVAR_HASH_LOGGING

static inline void AssertName([[maybe_unused]] const char* szName)
{
#ifdef _DEBUG
    assert(szName);

    // test for good console variable / command name
    const char* p = szName;
    bool bFirstChar = true;

    while (*p)
    {
        assert((*p >= 'a' && *p <= 'z')
            || (*p >= 'A' && *p <= 'Z')
            || (*p >= '0' && *p <= '9' && !bFirstChar)
            || *p == '_'
            || *p == '.');

        ++p;
        bFirstChar = false;
    }
#endif
}

void ResetCVars(IConsoleCmdArgs*)
{
    if (gEnv->pSystem)
    {
        CXConsole* pConsole = static_cast<CXConsole*>(gEnv->pSystem->GetIConsole());
        if (pConsole)
        {
            pConsole->ResetCVarsToDefaults();
        }
    }
}

//////////////////////////////////////////////////////////////////////////
// user defined comparison - for nicer printout
inline int GetCharPrio(char x)
{
    if (x >= 'a' && x <= 'z')
    {
        x += 'A' - 'a';                 // make upper case
    }
    if (x == '_')
    {
        return 300;
    }
    else
    {
        return x;
    }
}

void Command_SetWaitSeconds(IConsoleCmdArgs* pCmd)
{
    CXConsole* pConsole = (CXConsole*)gEnv->pConsole;

    if (pCmd->GetArgCount() > 1)
    {
        pConsole->m_waitSeconds.SetSeconds(atof(pCmd->GetArg(1)));
        pConsole->m_waitSeconds += gEnv->pTimer->GetFrameStartTime();
    }
}

void Command_SetWaitFrames(IConsoleCmdArgs* pCmd)
{
    CXConsole* pConsole = (CXConsole*)gEnv->pConsole;

    if (pCmd->GetArgCount() > 1)
    {
        pConsole->m_waitFrames = max(0, atoi(pCmd->GetArg(1)));
    }
}

void ConsoleShow(IConsoleCmdArgs*)
{
    gEnv->pConsole->ShowConsole(true);
}
void ConsoleHide(IConsoleCmdArgs*)
{
    gEnv->pConsole->ShowConsole(false);
}

void Bind(IConsoleCmdArgs* cmdArgs)
{
    if (cmdArgs->GetArgCount() >= 3)
    {
        AZStd::string arg;
        for (int i = 2; i < cmdArgs->GetArgCount(); ++i)
        {
            arg += cmdArgs->GetArg(i);
            arg += " ";
        }
        gEnv->pConsole->CreateKeyBind(cmdArgs->GetArg(1), arg.c_str());
    }
}

//////////////////////////////////////////////////////////////////////////
int CXConsole::con_display_last_messages = 0;
int CXConsole::con_line_buffer_size = 500;
int CXConsole::con_showonload = 0;
int CXConsole::con_debug = 0;
int CXConsole::con_restricted = 0;

namespace
{
    const AzFramework::InputChannelId s_nullRepeatEventId("xconsole_null_repeat_event_id");
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
CXConsole::CXConsole()
    : AzFramework::InputChannelEventListener(AzFramework::InputChannelEventListener::GetPriorityDebug())
    , AzFramework::InputTextEventListener(AzFramework::InputTextEventListener::GetPriorityDebug())
    , m_nRepeatEventId(s_nullRepeatEventId)
{
    m_fRepeatTimer = 0;
    m_pSysDeactivateConsole = 0;
    m_pFont = NULL;
    m_pImage = NULL;
    m_nCursorPos = 0;
    m_nScrollPos = 0;
    m_nScrollMax = 300;
    m_nTempScrollMax = m_nScrollMax;
    m_nScrollLine = 0;
    m_nHistoryPos = -1;
    m_nTabCount = 0;
    m_bConsoleActive = false;
    m_bActivationKeyEnable = true;
    m_bIsProcessingGroup = false;
    m_bIsConsoleKeyPressed = false;
    m_sdScrollDir = sdNONE;
    m_pSystem = NULL;
    m_bDrawCursor = true;
    m_fCursorBlinkTimer = 0;

    m_nCheatHashRangeFirst = 0;
    m_nCheatHashRangeLast = 0;
    m_bCheatHashDirty = false;
    m_nCheatHash = 0;

    m_bStaticBackground = false;
    m_nProgress = 0;
    m_nProgressRange = 0;
    m_nLoadingBackTexID = 0;

    m_deferredExecution = false;
    m_waitFrames = 0;
    m_waitSeconds = 0.0f;
    m_blockCounter = 0;

    AzFramework::ConsoleRequestBus::Handler::BusConnect();
    AzFramework::CommandRegistrationBus::Handler::BusConnect();

    AddCommand("resetcvars", (ConsoleCommandFunc)ResetCVars, 0, "Resets all cvars to their initial values");
}


//////////////////////////////////////////////////////////////////////////
CXConsole::~CXConsole()
{
    AzFramework::ConsoleRequestBus::Handler::BusDisconnect();
    AzFramework::CommandRegistrationBus::Handler::BusDisconnect();

    if (gEnv->pSystem)
    {
        gEnv->pSystem->GetIRemoteConsole()->UnregisterListener(this);
    }

    if (!m_mapVariables.empty())
    {
        while (!m_mapVariables.empty())
        {
            m_mapVariables.begin()->second->Release();
        }

        m_mapVariables.clear();
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::FreeRenderResources()
{
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::Release()
{
    delete this;
}


#if ALLOW_AUDIT_CVARS
void Command_AuditCVars(IConsoleCmdArgs* pArg)
{
    CXConsole* pConsole = (CXConsole*)gEnv->pConsole;
    if (pConsole != NULL)
    {
        pConsole->AuditCVars(pArg);
    }
}
#endif // ALLOW_AUDIT_CVARS



#if !defined(_RELEASE) && !defined(LINUX) && !defined(APPLE)
void Command_DumpCommandsVars(IConsoleCmdArgs* Cmd)
{
    const char* arg = "";

    if (Cmd->GetArgCount() > 1)
    {
        arg = Cmd->GetArg(1);
    }

    CXConsole* pConsole = (CXConsole*)gEnv->pConsole;

    // txt
    pConsole->DumpCommandsVarsTxt(arg);

#if defined(WIN32) || defined(WIN64)
    // HTML
    {
        CConsoleHelpGen Generator(*pConsole);

        Generator.Work();
    }
#endif
}

void Command_DumpVars(IConsoleCmdArgs* Cmd)
{
    bool includeCheat = false;

    if (Cmd->GetArgCount() > 1)
    {
        const char* arg = Cmd->GetArg(1);
        int incCheat = atoi(arg);
        if (incCheat == 1)
        {
            includeCheat = true;
        }
    }

    CXConsole* pConsole = (CXConsole*)gEnv->pConsole;

    // txt
    pConsole->DumpVarsTxt(includeCheat);
}
#endif

//////////////////////////////////////////////////////////////////////////
void CXConsole::Init(ISystem* pSystem)
{
    m_pSystem = static_cast<CSystem*>(pSystem);
    if (pSystem->GetICryFont())
    {
        m_pFont = pSystem->GetICryFont()->GetFont("default");
    }
    m_pTimer = pSystem->GetITimer();

    AzFramework::InputChannelEventListener::Connect();
    AzFramework::InputTextEventListener::Connect();

#if defined(_RELEASE) && !defined(PERFORMANCE_BUILD)
    static const int kDeactivateConsoleDefault = 1;
#else
    static const int kDeactivateConsoleDefault = 0;
#endif
    m_pSysDeactivateConsole = REGISTER_INT("sys_DeactivateConsole", kDeactivateConsoleDefault, 0,
            "0: normal console behavior\n"
            "1: hide the console");

    REGISTER_CVAR(con_display_last_messages, 0, VF_NULL, "");  // keep default at 1, needed for gameplay
    REGISTER_CVAR(con_line_buffer_size, 1000, VF_NULL, "");
    REGISTER_CVAR(con_showonload, 0, VF_NULL, "Show console on level loading");
    REGISTER_CVAR(con_debug, 0, VF_CHEAT, "Log call stack on every GetCVar call");
    REGISTER_CVAR(con_restricted, con_restricted, VF_RESTRICTEDMODE, "0=normal mode / 1=restricted access to the console");                // later on VF_RESTRICTEDMODE should be removed (to 0)

    if (m_pSystem->IsDevMode()       // unrestricted console for -DEVMODE
        || gEnv->IsDedicated())     // unrestricted console for dedicated server
    {
        con_restricted = 0;
    }

    m_nLoadingBackTexID = -1;

    if (gEnv->IsDedicated())
    {
        m_bConsoleActive = true;
    }

    REGISTER_COMMAND("ConsoleShow", &ConsoleShow, VF_NULL, "Opens the console");
    REGISTER_COMMAND("ConsoleHide", &ConsoleHide, VF_NULL, "Closes the console");

#if ALLOW_AUDIT_CVARS
    REGISTER_COMMAND("audit_cvars", &Command_AuditCVars, VF_NULL, "Logs all console commands and cvars");
#endif // ALLOW_AUDIT_CVARS

#if !defined(_RELEASE) && !defined(LINUX) && !defined(APPLE)
    REGISTER_COMMAND("DumpCommandsVars", &Command_DumpCommandsVars, VF_NULL,
        "This console command dumps all console variables and commands to disk\n"
        "DumpCommandsVars [prefix]");
    REGISTER_COMMAND("DumpVars", &Command_DumpVars, VF_NULL,
        "This console command dumps all console variables to disk\n"
        "DumpVars [IncludeCheatCvars]");
#endif

    REGISTER_COMMAND("Bind", &Bind, VF_NULL, "");
    REGISTER_COMMAND("wait_seconds", &Command_SetWaitSeconds, VF_BLOCKFRAME,
        "Forces the console to wait for a given number of seconds before the next deferred command is processed\n"
        "Works only in deferred command mode");
    REGISTER_COMMAND("wait_frames", &Command_SetWaitFrames, VF_BLOCKFRAME,
        "Forces the console to wait for a given number of frames before the next deferred command is processed\n"
        "Works only in deferred command mode");

    CConsoleBatchFile::Init();

    if (con_showonload)
    {
        ShowConsole(true);
    }

    pSystem->GetIRemoteConsole()->RegisterListener(this, "CXConsole");
}

void CXConsole::LogChangeMessage(const char* name, const bool isConst, const bool isCheat, const bool isReadOnly, const bool isDeprecated,
    const char* oldValue, const char* newValue, [[maybe_unused]] const bool isProcessingGroup, const bool allowChange)
{
    AZStd::string logMessage = AZStd::string::format
        ("[CVARS]: [%s] variable [%s] from [%s] to [%s]%s; Marked as%s%s%s%s",
        (allowChange) ? "CHANGED" : "IGNORED CHANGE",
        name,
        oldValue,
        newValue,
        (m_bIsProcessingGroup) ? " as part of a cvar group" : "",
        (isConst) ? " [VF_CONST_CVAR]" : "",
        (isCheat) ? " [VF_CHEAT]" : "",
        (isReadOnly) ? " [VF_READONLY]" : "",
        (isDeprecated) ? " [VF_DEPRECATED]" : "");

    if (allowChange)
    {
        gEnv->pLog->LogWarning(logMessage.c_str());
        gEnv->pLog->LogWarning("Modifying marked variables will not be allowed in Release mode!");
    }
    else
    {
        gEnv->pLog->LogError(logMessage.c_str());
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::RegisterVar(ICVar* pCVar, ConsoleVarFunc pChangeFunc)
{
    // first register callback so setting the value from m_configVars
    // is calling pChangeFunc         (that would be more correct but to not introduce new problems this code was not changed)
    //  if (pChangeFunc)
    //      pCVar->SetOnChangeCallback(pChangeFunc);

    bool isConst = pCVar->IsConstCVar();
    bool isCheat = ((pCVar->GetFlags() & (VF_CHEAT | VF_CHEAT_NOCHECK | VF_CHEAT_ALWAYS_CHECK)) != 0);
    bool isReadOnly = ((pCVar->GetFlags() & VF_READONLY) != 0);
    bool isDeprecated = ((pCVar->GetFlags() & VF_DEPRECATED) != 0);

    ConfigVars::iterator it = m_configVars.find(pCVar->GetName());
    if (it != m_configVars.end())
    {
        SConfigVar& var = it->second;
        bool allowChange = true;
        bool wasProcessingGroup = GetIsProcessingGroup();
        SetProcessingGroup(var.m_partOfGroup);

        if (
#if CVAR_GROUPS_ARE_PRIVILEGED
            !m_bIsProcessingGroup &&
#endif // !CVAR_GROUPS_ARE_PRIVILEGED
            (isConst || isCheat || isReadOnly || isDeprecated))
        {
            allowChange = !isDeprecated && ((gEnv->pSystem->IsDevMode()) || (gEnv->IsEditor()));
            if ((strcmp(pCVar->GetString(), var.m_value.c_str()) != 0) && (!(gEnv->IsEditor()) || isDeprecated))
            {
#if LOG_CVAR_INFRACTIONS
                LogChangeMessage(pCVar->GetName(), isConst, isCheat,
                    isReadOnly, isDeprecated, pCVar->GetString(), var.m_value.c_str(), m_bIsProcessingGroup, allowChange);
#if LOG_CVAR_INFRACTIONS_CALLSTACK
                gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
#endif // LOG_CVAR_INFRACTIONS
            }
        }

        if (allowChange || ALLOW_CONST_CVAR_MODIFICATIONS)
        {
            pCVar->Set(var.m_value.c_str());
            pCVar->SetFlags(pCVar->GetFlags() | VF_WASINCONFIG);
        }

        SetProcessingGroup(wasProcessingGroup);
    }
    else
    {
        // Variable is not modified when just registered.
        pCVar->ClearFlags(VF_MODIFIED);
    }

    if (pChangeFunc)
    {
        pCVar->SetOnChangeCallback(pChangeFunc);
    }

    ConsoleVariablesMapItor::value_type value = ConsoleVariablesMapItor::value_type(pCVar->GetName(), pCVar);

    m_mapVariables.insert(value);

    int flags = pCVar->GetFlags();

    if (flags & VF_CHEAT_ALWAYS_CHECK)
    {
        AddCheckedCVar(m_alwaysCheckedVariables, value);
    }
    else if ((flags & (VF_CHEAT | VF_CHEAT_NOCHECK)) == VF_CHEAT)
    {
        AddCheckedCVar(m_randomCheckedVariables, value);
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::AddCheckedCVar(ConsoleVariablesVector& vector, const ConsoleVariablesVector::value_type& value)
{
    ConsoleVariablesVector::iterator it = std::lower_bound(vector.begin(), vector.end(), value, CVarNameLess);

    if ((it == vector.end()) || strcmp(it->first, value.first))
    {
        vector.insert(it, value);
    }
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::CVarNameLess(const std::pair<const char*, ICVar*>& lhs, const std::pair<const char*, ICVar*>& rhs)
{
    return strcmp(lhs.first, rhs.first) < 0;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::LoadConfigVar(const char* sVariable, const char* sValue)
{
    ICVar* pCVar = GetCVar(sVariable);
    if (pCVar)
    {
        bool isConst = pCVar->IsConstCVar();
        bool isCheat = ((pCVar->GetFlags() & (VF_CHEAT | VF_CHEAT_NOCHECK | VF_CHEAT_ALWAYS_CHECK)) != 0);
        bool isReadOnly = ((pCVar->GetFlags() & VF_READONLY) != 0);
        bool isDeprecated = ((pCVar->GetFlags() & VF_DEPRECATED) != 0);
        bool allowChange = true;

        if (
#if CVAR_GROUPS_ARE_PRIVILEGED
            !m_bIsProcessingGroup &&
#endif // !CVAR_GROUPS_ARE_PRIVILEGED
            (isConst || isCheat || isReadOnly) || isDeprecated)
        {
            allowChange = !isDeprecated && (gEnv->pSystem->IsDevMode()) || (gEnv->IsEditor());
            if (!(gEnv->IsEditor()) || isDeprecated)
            {
#if LOG_CVAR_INFRACTIONS
                LogChangeMessage(pCVar->GetName(), isConst, isCheat,
                    isReadOnly, isDeprecated, pCVar->GetString(), sValue, m_bIsProcessingGroup, allowChange);
#if LOG_CVAR_INFRACTIONS_CALLSTACK
                gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
#endif // LOG_CVAR_INFRACTIONS
            }
        }

        if (allowChange || ALLOW_CONST_CVAR_MODIFICATIONS)
        {
            pCVar->Set(sValue);
            pCVar->SetFlags(pCVar->GetFlags() | VF_WASINCONFIG);
        }
        return;
    }

    SConfigVar temp;
    temp.m_value = sValue;
    temp.m_partOfGroup = m_bIsProcessingGroup;

    m_configVars[sVariable] = temp;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::EnableActivationKey(bool bEnable)
{
    m_bActivationKeyEnable = bEnable;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::SetClientDataProbeString(const char* pName, const char* pValue)
{
    ICVar*                  pCVar = GetCVar(pName);

    if (pCVar)
    {
        pCVar->SetDataProbeString(pValue);
    }
}


//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::Register(const char* sName, int* src, int iValue, int nFlags, const char* help, ConsoleVarFunc pChangeFunc, bool allowModify)
{
    AssertName(sName);

    ICVar* pCVar = stl::find_in_map(m_mapVariables, sName, NULL);
    if (pCVar)
    {
        gEnv->pLog->LogError("[CVARS]: [DUPLICATE] CXConsole::Register(int): variable [%s] is already registered", pCVar->GetName());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
        return pCVar;
    }

    if (!allowModify)
    {
        nFlags |= VF_CONST_CVAR;
    }
    pCVar = new CXConsoleVariableIntRef(this, sName, src, nFlags, help);
    *src = iValue;
    RegisterVar(pCVar, pChangeFunc);
    return pCVar;
}


//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::RegisterCVarGroup(const char* szName, const char* szFileName)
{
    AssertName(szName);
    assert(szFileName);

    // suppress cvars not starting with sys_spec_ as
    // cheaters might create cvars before we created ours
    if (_strnicmp(szName, "sys_spec_", 9) != 0)
    {
        return 0;
    }

    ICVar* pCVar = stl::find_in_map(m_mapVariables, szName, NULL);
    if (pCVar)
    {
        AZ_Error("System", false, "CVar groups should only be registered once");
        return pCVar;
    }

    CXConsoleVariableCVarGroup* pCVarGroup = new CXConsoleVariableCVarGroup(this, szName, szFileName, VF_COPYNAME);

    pCVar = pCVarGroup;

    RegisterVar(pCVar, CXConsoleVariableCVarGroup::OnCVarChangeFunc);

    return pCVar;
}

//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::Register(const char* sName, float* src, float fValue, int nFlags, const char* help, ConsoleVarFunc pChangeFunc, bool allowModify)
{
    AssertName(sName);

    ICVar* pCVar = stl::find_in_map(m_mapVariables, sName, NULL);
    if (pCVar)
    {
        gEnv->pLog->Log("[CVARS]: [DUPLICATE] CXConsole::Register(float): variable [%s] is already registered", pCVar->GetName());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
        return pCVar;
    }
    if (!allowModify)
    {
        nFlags |= VF_CONST_CVAR;
    }
    pCVar = new CXConsoleVariableFloatRef(this, sName, src, nFlags, help);
    *src = fValue;
    RegisterVar(pCVar, pChangeFunc);
    return pCVar;
}


//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::Register(const char* sName, const char** src, const char* defaultValue, int nFlags, const char* help, ConsoleVarFunc pChangeFunc, bool allowModify)
{
    AssertName(sName);

    ICVar* pCVar = stl::find_in_map(m_mapVariables, sName, NULL);
    if (pCVar)
    {
        gEnv->pLog->Log("[CVARS]: [DUPLICATE] CXConsole::Register(const char*): variable [%s] is already registered", pCVar->GetName());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
        return pCVar;
    }
    if (!allowModify)
    {
        nFlags |= VF_CONST_CVAR;
    }
    pCVar = new CXConsoleVariableStringRef(this, sName, src, defaultValue, nFlags, help);
    RegisterVar(pCVar, pChangeFunc);
    return pCVar;
}


//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::RegisterString(const char* sName, const char* sValue, int nFlags, const char* help, ConsoleVarFunc pChangeFunc)
{
    AssertName(sName);

    ICVar* pCVar = stl::find_in_map(m_mapVariables, sName, NULL);
    if (pCVar)
    {
        gEnv->pLog->Log("[CVARS]: [DUPLICATE] CXConsole::RegisterString(const char*): variable [%s] is already registered", pCVar->GetName());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
        return pCVar;
    }

    pCVar = new CXConsoleVariableString(this, sName, sValue, nFlags, help);
    RegisterVar(pCVar, pChangeFunc);
    return pCVar;
}

//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::RegisterFloat(const char* sName, float fValue, int nFlags, const char* help, ConsoleVarFunc pChangeFunc)
{
    AssertName(sName);

    ICVar* pCVar = stl::find_in_map(m_mapVariables, sName, NULL);
    if (pCVar)
    {
        gEnv->pLog->Log("[CVARS]: [DUPLICATE] CXConsole::RegisterFloat(): variable [%s] is already registered", pCVar->GetName());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
        return pCVar;
    }

    pCVar = new CXConsoleVariableFloat(this, sName, fValue, nFlags, help);
    RegisterVar(pCVar, pChangeFunc);
    return pCVar;
}

//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::RegisterInt(const char* sName, int iValue, int nFlags, const char* help, ConsoleVarFunc pChangeFunc)
{
    AssertName(sName);

    ICVar* pCVar = stl::find_in_map(m_mapVariables, sName, NULL);
    if (pCVar)
    {
        gEnv->pLog->Log("[CVARS]: [DUPLICATE] CXConsole::RegisterInt(): variable [%s] is already registered", pCVar->GetName());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
        return pCVar;
    }

    pCVar = new CXConsoleVariableInt(this, sName, iValue, nFlags, help);
    RegisterVar(pCVar, pChangeFunc);
    return pCVar;
}



//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::RegisterInt64(const char* sName, int64 iValue, int nFlags, const char* help, ConsoleVarFunc pChangeFunc)
{
    AssertName(sName);

    ICVar* pCVar = stl::find_in_map(m_mapVariables, sName, NULL);
    if (pCVar)
    {
        gEnv->pLog->Log("[CVARS]: [DUPLICATE] CXConsole::RegisterInt64(): variable [%s] is already registered", pCVar->GetName());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
        return pCVar;
    }

    pCVar = new CXConsoleVariableInt64(this, sName, iValue, nFlags, help);
    RegisterVar(pCVar, pChangeFunc);
    return pCVar;
}



//////////////////////////////////////////////////////////////////////////
void CXConsole::UnregisterVariable(const char* sVarName, [[maybe_unused]] bool bDelete)
{
    ConsoleVariablesMapItor itor;
    itor = m_mapVariables.find(sVarName);

    if (itor == m_mapVariables.end())
    {
        return;
    }

    ICVar* pCVar = itor->second;

    int32 flags = pCVar->GetFlags();

    if (flags & VF_CHEAT_ALWAYS_CHECK)
    {
        RemoveCheckedCVar(m_alwaysCheckedVariables, *itor);
    }
    else if ((flags & (VF_CHEAT | VF_CHEAT_NOCHECK)) == VF_CHEAT)
    {
        RemoveCheckedCVar(m_randomCheckedVariables, *itor);
    }

    m_mapVariables.erase(sVarName);

    delete pCVar;
}

void CXConsole::RemoveCheckedCVar(ConsoleVariablesVector& vector, const ConsoleVariablesVector::value_type& value)
{
    ConsoleVariablesVector::iterator it = std::lower_bound(vector.begin(), vector.end(), value, CVarNameLess);

    if ((it != vector.end()) && !strcmp(it->first, value.first))
    {
        vector.erase(it);
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::SetScrollMax(int value)
{
    m_nScrollMax = value;
    m_nTempScrollMax = m_nScrollMax;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CXConsole::SetImage(ITexture* pImage, bool bDeleteCurrent)
{
    if (bDeleteCurrent)
    {
        pImage->Release();
    }

    m_pImage = pImage;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
void    CXConsole::ShowConsole(bool show, const int iRequestScrollMax)
{
    if (m_pSysDeactivateConsole->GetIVal())
    {
        show = false;
    }

    if (show && !m_bConsoleActive)
    {
        UiCursorBus::Broadcast(&UiCursorBus::Events::IncrementVisibleCounter);

        AzFramework::InputSystemCursorRequestBus::EventResult(m_previousSystemCursorState,
            AzFramework::InputDeviceMouse::Id,
            &AzFramework::InputSystemCursorRequests::GetSystemCursorState);
        AzFramework::InputSystemCursorRequestBus::Event(AzFramework::InputDeviceMouse::Id,
            &AzFramework::InputSystemCursorRequests::SetSystemCursorState,
            AzFramework::SystemCursorState::UnconstrainedAndVisible);
    }
    else if (!show && m_bConsoleActive)
    {
        UiCursorBus::Broadcast(&UiCursorBus::Events::DecrementVisibleCounter);

        AzFramework::InputSystemCursorRequestBus::Event(AzFramework::InputDeviceMouse::Id,
            &AzFramework::InputSystemCursorRequests::SetSystemCursorState,
            m_previousSystemCursorState);
    }

    SetStatus(show);

    if (iRequestScrollMax > 0)
    {
        m_nTempScrollMax = iRequestScrollMax;         // temporary user request
    }
    else
    {
        m_nTempScrollMax = m_nScrollMax;                  // reset
    }
    if (m_bConsoleActive)
    {
        m_sdScrollDir = sdDOWN;
    }
    else
    {
        m_sdScrollDir = sdUP;
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::CreateKeyBind(const char* sCmd, const char* sRes)
{
    m_mapBinds.insert(ConsoleBindsMapItor::value_type(sCmd, sRes));
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::DumpKeyBinds(IKeyBindDumpSink* pCallback)
{
    for (ConsoleBindsMap::iterator it = m_mapBinds.begin(); it != m_mapBinds.end(); ++it)
    {
        pCallback->OnKeyBindFound(it->first.c_str(), it->second.c_str());
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* CXConsole::FindKeyBind(const char* sCmd) const
{
    ConsoleBindsMap::const_iterator it = m_mapBinds.find(sCmd);

    if (it != m_mapBinds.end())
    {
        return it->second.c_str();
    }

    return 0;
}


//////////////////////////////////////////////////////////////////////////
void CXConsole::DumpCVars(ICVarDumpSink* pCallback, unsigned int nFlagsFilter)
{
    ConsoleVariablesMapItor It = m_mapVariables.begin();

    while (It != m_mapVariables.end())
    {
        if ((nFlagsFilter == 0) || ((nFlagsFilter != 0) && (It->second->GetFlags() & nFlagsFilter)))
        {
            pCallback->OnElementFound(It->second);
        }
        ++It;
    }
}

//////////////////////////////////////////////////////////////////////////
ICVar* CXConsole::GetCVar(const char* sName)
{
    assert(this);
    assert(sName);

    if (con_debug)
    {
        // Log call stack on get cvar.
        CryLog("GetCVar(\"%s\") called", sName);
        m_pSystem->debug_LogCallStack();
    }

    // Fast map lookup for case-sensitive match.
    ConsoleVariablesMapItor it;

    it = m_mapVariables.find(sName);
    if (it != m_mapVariables.end())
    {
        return it->second;
    }

    /*
        if(!bCaseSensitive)
        {
            // Much slower but allows names with wrong case (use only where performance doesn't matter).
            for(it=m_mapVariables.begin(); it!=m_mapVariables.end(); ++it)
            {
                if(azstricmp(it->first,sName)==0)
                    return it->second;
            }
        }
        test else
        {
            for(it=m_mapVariables.begin(); it!=m_mapVariables.end(); ++it)
            {
                if(azstricmp(it->first,sName)==0)
                {
                    CryFatalError("Error: Wrong case for '%s','%s'",it->first,sName);
                }
            }
        }
    */

    return NULL;        // haven't found this name
}

//////////////////////////////////////////////////////////////////////////
char* CXConsole::GetVariable([[maybe_unused]] const char* szVarName, [[maybe_unused]] const char* szFileName, [[maybe_unused]] const char* def_val)
{
    assert(m_pSystem);
    return 0;
}

//////////////////////////////////////////////////////////////////////////
float CXConsole::GetVariable([[maybe_unused]] const char* szVarName, [[maybe_unused]] const char* szFileName, [[maybe_unused]] float def_val)
{
    assert(m_pSystem);
    return 0;
}


//////////////////////////////////////////////////////////////////////////
bool CXConsole::GetStatus()
{
    return m_bConsoleActive;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::Clear()
{
    m_dqConsoleBuffer.clear();
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::Update()
{
    if (!m_pSystem)
    {
        return;
    }

    if (m_bIsConsoleKeyPressed)
    {
        m_sInputBuffer.clear();
        m_nCursorPos = 0;
        m_bIsConsoleKeyPressed = false;
    }

    // Execute the deferred commands
    ExecuteDeferredCommands();

    if (!m_bConsoleActive)
    {
        m_nRepeatEventId = s_nullRepeatEventId;
    }

    // Process Key press repeat (backspace and cursor on PC)
    if (m_nRepeatEventId != s_nullRepeatEventId)
    {
        const float fRepeatDelay = 1.0f / 40.0f;          // in sec (similar to Windows default but might differ from actual setting)
        const float fHitchDelay = 1.0f / 10.0f;               // in sec. Very low, but still reasonable frame-rate (debug builds)

        m_fRepeatTimer -= gEnv->pTimer->GetRealFrameTime();                                         // works even when time is manipulated
        //      m_fRepeatTimer -= gEnv->pTimer->GetFrameTime(ITimer::ETIMER_UI);            // can be used once ETIMER_UI works even with t_FixedTime

        if (m_fRepeatTimer <= 0.0f)
        {
            if (m_fRepeatTimer < -fHitchDelay)
            {
                // bad framerate or hitch
                m_nRepeatEventId = s_nullRepeatEventId;
            }
            else
            {
                const AzFramework::InputChannel* repeatInputChannel = AzFramework::InputChannelRequests::FindInputChannel(m_nRepeatEventId);
                if (repeatInputChannel)
                {
                    ProcessInput(*repeatInputChannel);
                }
                m_fRepeatTimer = fRepeatDelay;            // next repeat even in .. sec
            }
        }
    }
}

//enable this for now, we need it for profiling etc
//MUST DISABLE FOR TCG BUILDS
#   define PROCESS_XCONSOLE_INPUT

//////////////////////////////////////////////////////////////////////////
bool CXConsole::OnInputChannelEventFiltered(const AzFramework::InputChannel& inputChannel)
{
#ifdef PROCESS_XCONSOLE_INPUT
    const AzFramework::InputDeviceId& deviceId = inputChannel.GetInputDevice().GetInputDeviceId();
    if (!AzFramework::InputDeviceKeyboard::IsKeyboardDevice(deviceId))
    {
        // Don't consume non-keyboard events
        return false;
    }

    if (inputChannel.IsStateEnded() && m_bConsoleActive)
    {
        m_nRepeatEventId = s_nullRepeatEventId;
    }

    if (!inputChannel.IsStateBegan())
    {
        // Consume keyboard events if the console is active
        return m_bConsoleActive;
    }

    const AzFramework::InputChannelId& channelId = inputChannel.GetInputChannelId();
    const AzFramework::ModifierKeyStates* customData = inputChannel.GetCustomData<AzFramework::ModifierKeyStates>();
    const AzFramework::ModifierKeyStates modifierKeyStates = customData ? *customData : AzFramework::ModifierKeyStates();

    // restart cursor blinking
    m_fCursorBlinkTimer = 0.0f;
    m_bDrawCursor = true;

    // key repeat
    const float fStartRepeatDelay = 0.5f;                       // in sec (similar to Windows default but might differ from actual setting)
    m_nRepeatEventId = channelId;
    m_fRepeatTimer = fStartRepeatDelay;

    //execute Binds
    if (!m_bConsoleActive)
    {
        const char* cmd = 0;

        if (modifierKeyStates.GetActiveModifierKeys() == AzFramework::ModifierKeyMask::None)
        {
            // fast
            cmd = FindKeyBind(channelId.GetName());
        }
        else
        {
            // slower
            char szCombinedName[255];
            int iLen = 0;

            if (modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::CtrlAny))
            {
                azstrcpy(szCombinedName, AZ_ARRAY_SIZE(szCombinedName), "ctrl_");
                iLen += 5;
            }
            if (modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::ShiftAny))
            {
                azstrcpy(&szCombinedName[iLen], AZ_ARRAY_SIZE(szCombinedName) - iLen, "shift_");
                iLen += 6;
            }
            if (modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::AltAny))
            {
                azstrcpy(&szCombinedName[iLen], AZ_ARRAY_SIZE(szCombinedName) - iLen, "alt_");
                iLen += 4;
            }
            if (modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::SuperAny))
            {
                azstrcpy(&szCombinedName[iLen], AZ_ARRAY_SIZE(szCombinedName) - iLen, "win_");
                iLen += 4;
            }

            assert(sizeof(szCombinedName) > (iLen + strlen(channelId.GetName()) + 1));
            azstrcpy(&szCombinedName[iLen], AZ_ARRAY_SIZE(szCombinedName) - iLen, channelId.GetName());

            cmd = FindKeyBind(szCombinedName);
        }

        if (cmd)
        {
            SetInputLine("");
            ExecuteStringInternal(cmd, true);        // keybinds are treated as they would come from console
        }
    }
    else
    {
        if (channelId != AzFramework::InputDeviceKeyboard::Key::EditTab)
        {
            ResetAutoCompletion();
        }

        if (channelId == AzFramework::InputDeviceKeyboard::Key::AlphanumericC &&
            modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::CtrlAny))
        {
            Copy();

            // Consume keyboard events if the console is active, which it will be if we get here
            return true;
        }
        else if (channelId == AzFramework::InputDeviceKeyboard::Key::AlphanumericV &&
            modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::CtrlAny))
        {
            Paste();
            return true;
        }
    }

    if (channelId == AzFramework::InputDeviceKeyboard::Key::PunctuationTilde)
    {
        if (m_bActivationKeyEnable)
        {
            ShowConsole(!GetStatus());
            m_nRepeatEventId = s_nullRepeatEventId;
            m_bIsConsoleKeyPressed = true;
            return true;
        }
    }
    if (channelId == AzFramework::InputDeviceKeyboard::Key::Escape)
    {
        // hide console if it's active
        if (GetStatus())
        {
            ShowConsole(false);
            m_bIsConsoleKeyPressed = true;
            return true;
        }

        //switch process or page or other things
        if (m_pSystem)
        {
            ISystemUserCallback* pCallback = ((CSystem*)m_pSystem)->GetUserCallback();
            if (pCallback)
            {
                pCallback->OnProcessSwitch();
                m_bIsConsoleKeyPressed = true;
                // Mark this input as handled. Pressing escape here is used in the editor to exit game mode, and return to edit mode.
                // If AI/Physics mode was enabled before entering game mode, when returning to edit mode it will be enabled again.
                // When it is enabled, it will reset input. If this returns false, then other handlers on the ebus would continue to process
                // input events after the input had been reset. By returning true, the input is marked as handled.
                return true;
            }
        }
    }

    return ProcessInput(inputChannel);

#else

    return false;

#endif
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::OnInputTextEventFiltered(const AZStd::string& textUTF8)
{
#ifdef PROCESS_XCONSOLE_INPUT
    // Ignore tilde/accent/power of two character since it is reserved for toggling the console
    const bool isTilde = (textUTF8 == "~" || textUTF8 == "`" || textUTF8 == "\xC2\xB2");
    if (m_bConsoleActive && !isTilde)
    {
        AddInputUTF8(textUTF8);
    }
#endif
    return false;
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::ProcessInput(const AzFramework::InputChannel& inputChannel)
{
#ifdef PROCESS_XCONSOLE_INPUT

    if (!m_bConsoleActive)
    {
        return false;
    }

    const AzFramework::InputChannelId& channelId = inputChannel.GetInputChannelId();
    const AzFramework::ModifierKeyStates* customData = inputChannel.GetCustomData<AzFramework::ModifierKeyStates>();
    const AzFramework::ModifierKeyStates modifierKeyStates = customData ? *customData : AzFramework::ModifierKeyStates();

    const bool isAltModifierActive = modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::AltAny);
    const bool isCtrlModifierActive = modifierKeyStates.IsActive(AzFramework::ModifierKeyMask::CtrlAny);

    if (channelId == AzFramework::InputDeviceKeyboard::Key::EditEnter ||
        channelId == AzFramework::InputDeviceKeyboard::Key::NumPadEnter)
    {
        ExecuteInputBuffer();
        m_nScrollLine = 0;
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::EditBackspace)
    {
        RemoveInputChar(true);
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationArrowLeft)
    {
        if (m_nCursorPos)
        {
            const char* pCursor = m_sInputBuffer.c_str() + m_nCursorPos;
            pCursor -= Utf8::Internal::sequence_length(pCursor); // Note: This moves back one UCS code-point, but doesn't necessarily match one displayed character (ie, combining diacritics)
            m_nCursorPos = pCursor - m_sInputBuffer.c_str();
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationArrowRight)
    {
        if (m_nCursorPos < (int)(m_sInputBuffer.length()))
        {
            const char* pCursor = m_sInputBuffer.c_str() + m_nCursorPos;
            pCursor += Utf8::Internal::sequence_length(pCursor); // Note: This moves forward one UCS code-point, but doesn't necessarily match one displayed character (ie, combining diacritics)
            m_nCursorPos = pCursor - m_sInputBuffer.c_str();
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationArrowUp)
    {
        const char* szHistoryLine = GetHistoryElement(true);      // true=UP

        if (szHistoryLine)
        {
            m_sInputBuffer = szHistoryLine;
            m_nCursorPos = (int)m_sInputBuffer.size();
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationArrowDown)
    {
        const char* szHistoryLine = GetHistoryElement(false);     // false=DOWN

        if (szHistoryLine)
        {
            m_sInputBuffer = szHistoryLine;
            m_nCursorPos = (int)m_sInputBuffer.size();
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::EditTab)
    {
        if (!isAltModifierActive)
        {
            m_sInputBuffer = ProcessCompletion(m_sInputBuffer.c_str());
            m_nCursorPos = m_sInputBuffer.size();
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationPageUp)
    {
        if (isCtrlModifierActive)
        {
            m_nScrollLine = min((int)(m_dqConsoleBuffer.size() - 1), m_nScrollLine + 21);
        }
        else
        {
            m_nScrollLine = min((int)(m_dqConsoleBuffer.size() - 1), m_nScrollLine + 1);
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationPageDown)
    {
        if (isCtrlModifierActive)
        {
            m_nScrollLine = max(0, m_nScrollLine - 21);
        }
        else
        {
            m_nScrollLine = max(0, m_nScrollLine - 1);
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationHome)
    {
        if (isCtrlModifierActive)
        {
            m_nScrollLine = static_cast<int>(m_dqConsoleBuffer.size() - 1);
        }
        else
        {
            m_nCursorPos = 0;
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationEnd)
    {
        if (isCtrlModifierActive)
        {
            m_nScrollLine = 0;
        }
        else
        {
            m_nCursorPos = (int)m_sInputBuffer.length();
        }
        return true;
    }
    else if (channelId == AzFramework::InputDeviceKeyboard::Key::NavigationDelete)
    {
        RemoveInputChar(false);
        return true;
    }

#endif

    // Consume keyboard events if the console is active, which it will be if we get here
    return true;
}

#ifdef PROCESS_XCONSOLE_INPUT
#   undef PROCESS_XCONSOLE_INPUT
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
void CXConsole::OnConsoleCommand(const char* cmd)
{
    ExecuteString(cmd, false);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
const char* CXConsole::GetHistoryElement(const bool bUpOrDown)
{
    if (bUpOrDown)
    {
        if (!m_dqHistory.empty())
        {
            if (m_nHistoryPos < (int)(m_dqHistory.size() - 1))
            {
                m_nHistoryPos++;
                m_sReturnString = m_dqHistory[m_nHistoryPos];
                return m_sReturnString.c_str();
            }
        }
    }
    else
    {
        if (m_nHistoryPos > 0)
        {
            m_nHistoryPos--;
            m_sReturnString = m_dqHistory[m_nHistoryPos];
            return m_sReturnString.c_str();
        }
    }

    return 0;
}



//////////////////////////////////////////////////////////////////////////
void CXConsole::Draw()
{
}

void CXConsole::DrawBuffer(int, const char*)
{
}


//////////////////////////////////////////////////////////////////////////
bool CXConsole::GetLineNo(const int indwLineNo, char* outszBuffer, const int indwBufferSize) const
{
    assert(outszBuffer);
    assert(indwBufferSize > 0);

    outszBuffer[0] = 0;

    ConsoleBuffer::const_reverse_iterator ritor = m_dqConsoleBuffer.rbegin();

    ritor += indwLineNo;

    if (indwLineNo >= (int)m_dqConsoleBuffer.size())
    {
        return false;
    }

    const char* buf = ritor->c_str();// GetBuf(k);

    if (*buf > 0 && *buf < 32)
    {
        buf++;                          // to jump over verbosity level character
    }
    azstrcpy(outszBuffer, indwBufferSize, buf);

    return true;
}

//////////////////////////////////////////////////////////////////////////
int CXConsole::GetLineCount() const
{
    return static_cast<int>(m_dqConsoleBuffer.size());
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ScrollConsole()
{
}


//////////////////////////////////////////////////////////////////////////
bool CXConsole::AddCommand(const char* sCommand, ConsoleCommandFunc func, int nFlags, const char* sHelp)
{
    AssertName(sCommand);

    if (m_mapCommands.find(sCommand) == m_mapCommands.end())
    {
        CConsoleCommand cmd;
        cmd.m_sName = sCommand;
        cmd.m_func = func;
        if (sHelp)
        {
            cmd.m_sHelp = sHelp;
        }
        cmd.m_nFlags = nFlags;
        m_mapCommands.insert(std::make_pair(cmd.m_sName, cmd));

        return true;
    }
    else
    {
        gEnv->pLog->LogError("[CVARS]: [DUPLICATE] CXConsole::AddCommand(): console command [%s] is already registered", sCommand);
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK

        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::AddCommand(const char* sCommand, const char* sScriptFunc, int nFlags, const char* sHelp)
{
    AssertName(sCommand);

    if (m_mapCommands.find(sCommand) == m_mapCommands.end())
    {
        CConsoleCommand cmd;
        cmd.m_sName = sCommand;
        cmd.m_sCommand = sScriptFunc;
        if (sHelp)
        {
            cmd.m_sHelp = sHelp;
        }
        cmd.m_nFlags = nFlags;
        m_mapCommands.insert(std::make_pair(cmd.m_sName, cmd));

        return true;
    }
    else
    {
        gEnv->pLog->LogError("[CVARS]: [DUPLICATE] CXConsole::AddCommand(): script command [%s] is already registered", sCommand);
#if LOG_CVAR_INFRACTIONS_CALLSTACK
        gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK

        return false;
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::RemoveCommand(const char* sName)
{
    ConsoleCommandsMap::iterator ite = m_mapCommands.find(sName);
    if (ite != m_mapCommands.end())
    {
        m_mapCommands.erase(ite);
    }
}

//////////////////////////////////////////////////////////////////////////
inline bool hasprefix(const char* s, const char* prefix)
{
    while (*prefix)
    {
        if (*prefix++ != *s++)
        {
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
const char* CXConsole::GetFlagsString(const uint32 dwFlags)
{
    static char sFlags[256];

    // hiding this makes it a bit more difficult for cheaters
    //  if(dwFlags&VF_CHEAT)                  azstrcat( sFlags,"CHEAT, ");

    azstrcpy(sFlags, AZ_ARRAY_SIZE(sFlags), "");

    if (dwFlags & VF_READONLY)
    {
        azstrcat(sFlags, AZ_ARRAY_SIZE(sFlags), "READONLY, ");
    }
    if (dwFlags & VF_DEPRECATED)
    {
        azstrcat(sFlags, AZ_ARRAY_SIZE(sFlags), "DEPRECATED, ");
    }
    if (dwFlags & VF_DUMPTODISK)
    {
        azstrcat(sFlags, AZ_ARRAY_SIZE(sFlags), "DUMPTODISK, ");
    }
    if (dwFlags & VF_REQUIRE_LEVEL_RELOAD)
    {
        azstrcat(sFlags, AZ_ARRAY_SIZE(sFlags), "REQUIRE_LEVEL_RELOAD, ");
    }
    if (dwFlags & VF_REQUIRE_APP_RESTART)
    {
        azstrcat(sFlags, AZ_ARRAY_SIZE(sFlags), "REQUIRE_APP_RESTART, ");
    }
    if (dwFlags & VF_RESTRICTEDMODE)
    {
        azstrcat(sFlags, AZ_ARRAY_SIZE(sFlags), "RESTRICTEDMODE, ");
    }

    if (sFlags[0] != 0)
    {
        sFlags[strlen(sFlags) - 2] = 0;  // remove ending ", "
    }
    return sFlags;
}

#if ALLOW_AUDIT_CVARS
void CXConsole::AuditCVars(IConsoleCmdArgs* pArg)
{
    int numArgs = pArg->GetArgCount();
    int cheatMask = VF_CHEAT | VF_CHEAT_NOCHECK | VF_CHEAT_ALWAYS_CHECK;
    int constMask = VF_CONST_CVAR;
    int readOnlyMask = VF_READONLY;
    int devOnlyMask = VF_DEV_ONLY;
    int dediOnlyMask = VF_DEDI_ONLY;
    int excludeMask = cheatMask | constMask | readOnlyMask | devOnlyMask | dediOnlyMask;

    if (numArgs > 1)
    {
        while (numArgs > 1)
        {
            const char* arg = pArg->GetArg(numArgs - 1);

            if (azstricmp(arg, "cheat") == 0)
            {
                excludeMask &= ~cheatMask;
            }

            if (azstricmp(arg, "const") == 0)
            {
                excludeMask &= ~constMask;
            }

            if (azstricmp(arg, "readonly") == 0)
            {
                excludeMask &= ~readOnlyMask;
            }

            if (azstricmp(arg, "dev") == 0)
            {
                excludeMask &= ~devOnlyMask;
            }

            if (azstricmp(arg, "dedi") == 0)
            {
                excludeMask &= ~dediOnlyMask;
            }

            --numArgs;
        }
    }

    int commandCount = 0;
    int cvarCount = 0;

    CryLogAlways("[CVARS]: [BEGIN AUDIT]");

    for (ConsoleCommandsMapItor it = m_mapCommands.begin(); it != m_mapCommands.end(); ++it)
    {
        CConsoleCommand& command = it->second;

        int cheatFlags = (command.m_nFlags & cheatMask);
        int devOnlyFlags = (command.m_nFlags & devOnlyMask);
        int dediOnlyFlags = (command.m_nFlags & dediOnlyMask);
        bool shouldLog = ((cheatFlags | devOnlyFlags | dediOnlyFlags) == 0) || (((cheatFlags | devOnlyFlags | dediOnlyFlags) & ~excludeMask) != 0);
        if (shouldLog)
        {
            CryLogAlways("[CVARS]: [COMMAND] %s%s%s%s%s",
                command.m_sName.c_str(),
                (cheatFlags != 0) ? " [VF_CHEAT]" : "",
                (devOnlyFlags != 0) ? " [VF_DEV_ONLY]" : "",
                (dediOnlyFlags != 0) ? " [VF_DEDI_ONLY]" : "",
                ""
                );
            ++commandCount;
        }
    }

    for (ConsoleVariablesMapItor it = m_mapVariables.begin(); it != m_mapVariables.end(); ++it)
    {
        ICVar* pVariable = it->second;
        int flags = pVariable->GetFlags();

        int cheatFlags = (flags & cheatMask);
        int constFlags = (flags & constMask);
        int readOnlyFlags = (flags & readOnlyMask);
        int devOnlyFlags = (flags & devOnlyMask);
        int dediOnlyFlags = (flags & dediOnlyMask);
        bool shouldLog = ((cheatFlags | constFlags | readOnlyFlags | devOnlyFlags | dediOnlyFlags) == 0) || (((cheatFlags | constFlags | readOnlyFlags | devOnlyFlags | dediOnlyFlags) & ~excludeMask) != 0);
        if (shouldLog)
        {
            CryLogAlways("[CVARS]: [VARIABLE] %s%s%s%s%s%s%s",
                pVariable->GetName(),
                (cheatFlags != 0) ? " [VF_CHEAT]" : "",
                (constFlags != 0) ? " [VF_CONST_CVAR]" : "",
                (readOnlyFlags != 0) ? " [VF_READONLY]" : "",
                (devOnlyFlags != 0) ? " [VF_DEV_ONLY]" : "",
                (dediOnlyFlags != 0) ? " [VF_DEDI_ONLY]" : "",
                ""
                );
            ++cvarCount;
        }
    }

    CryLogAlways("[CVARS]: [END AUDIT] (commands %d/%" PRISIZE_T "; variables %d/%" PRISIZE_T ")", commandCount, m_mapCommands.size(), cvarCount, m_mapVariables.size());
}
#endif // ALLOW_AUDIT_CVARS

//////////////////////////////////////////////////////////////////////////
#ifndef _RELEASE
void CXConsole::DumpCommandsVarsTxt(const char* prefix)
{
    FILE* f0 = nullptr;
    azfopen(&f0, "consolecommandsandvars.txt", "w");

    if (!f0)
    {
        return;
    }

    ConsoleCommandsMapItor itrCmd, itrCmdEnd = m_mapCommands.end();
    ConsoleVariablesMapItor itrVar, itrVarEnd = m_mapVariables.end();

    fprintf(f0, " CHEAT: stays in the default value if cheats are not disabled\n");
    fprintf(f0, " REQUIRE_NET_SYNC: cannot be changed on client and when connecting it's sent to the client\n");
    fprintf(f0, " SAVEGAME: stored when saving a savegame\n");
    fprintf(f0, " READONLY: can not be changed by the user\n");
    fprintf(f0, "-------------------------\n");
    fprintf(f0, "\n");

    for (itrCmd = m_mapCommands.begin(); itrCmd != itrCmdEnd; ++itrCmd)
    {
        CConsoleCommand& cmd = itrCmd->second;

        if (hasprefix(cmd.m_sName.c_str(), prefix))
        {
            const char* sFlags = GetFlagsString(cmd.m_nFlags);

            fprintf(f0, "Command: %s %s\nscript: %s\nhelp: %s\n\n", cmd.m_sName.c_str(), sFlags, cmd.m_sCommand.c_str(), cmd.m_sHelp.c_str());
        }
    }

    for (itrVar = m_mapVariables.begin(); itrVar != itrVarEnd; ++itrVar)
    {
        ICVar* var = itrVar->second;
        const char* types[] = { "?", "int", "float", "string", "?" };

        var->GetRealIVal();         // assert inside checks consistency for all cvars

        if (hasprefix(var->GetName(), prefix))
        {
            const char* sFlags = GetFlagsString(var->GetFlags());

            fprintf(f0, "variable: %s %s\ntype: %s\ncurrent: %s\nhelp: %s\n\n", var->GetName(), sFlags, types[var->GetType()], var->GetString(), var->GetHelp());
        }
    }

    fclose(f0);

    ConsoleLogInputResponse("successfully wrote consolecommandsandvars.txt");
}

void CXConsole::DumpVarsTxt(const bool includeCheat)
{
    FILE* f0 = nullptr;
    azfopen(&f0, "consolevars.txt", "w");
    if (!f0)
    {
        return;
    }

    ConsoleVariablesMapItor itrVar, itrVarEnd = m_mapVariables.end();

    fprintf(f0, " REQUIRE_NET_SYNC: cannot be changed on client and when connecting it's sent to the client\n");
    fprintf(f0, " SAVEGAME: stored when saving a savegame\n");
    fprintf(f0, " READONLY: can not be changed by the user\n");
    fprintf(f0, "-------------------------\n");
    fprintf(f0, "\n");

    for (itrVar = m_mapVariables.begin(); itrVar != itrVarEnd; ++itrVar)
    {
        ICVar* var = itrVar->second;
        const char* types[] = { "?", "int", "float", "string", "?" };

        var->GetRealIVal();         // assert inside checks consistency for all cvars
        const int flags = var->GetFlags();

        if ((includeCheat == true) || (flags & VF_CHEAT) == 0)
        {
            const char* sFlags = GetFlagsString(flags);
            fprintf(f0, "variable: %s %s\ntype: %s\ncurrent: %s\nhelp: %s\n\n", var->GetName(), sFlags, types[var->GetType()], var->GetString(), var->GetHelp());
        }
    }

    fclose(f0);

    ConsoleLogInputResponse("successfully wrote consolevars.txt");
}
#endif

//////////////////////////////////////////////////////////////////////////
void CXConsole::DisplayHelp(const char* help, const char* name)
{
    if (help == 0 || *help == 0)
    {
        ConsoleLogInputResponse("No help available for $3%s", name);
    }
    else
    {
        char* start, * pos;
        for (pos = strstr((char*)help, "\n"), start = (char*)help; pos; start = ++pos)
        {
            AZStd::string s = start;
            s.resize(pos - start);
            ConsoleLogInputResponse("    $3%s", s.c_str());
            pos = strstr(pos, "\n");
        }
        ConsoleLogInputResponse("    $3%s", start);
    }
}


void CXConsole::ExecuteString(const char* command, const bool bSilentMode, const bool bDeferExecution)
{
    if (!m_deferredExecution && !bDeferExecution)
    {
        // This is a regular mode
        ExecuteStringInternal(command, false, bSilentMode);     // not from console

        return;
    }

    // Store the string commands into a list and defer the execution for later.
    // The commands will be processed in CXConsole::Update()
    AZStd::string str(command);
    AZ::StringFunc::TrimWhiteSpace(str, true, false);

    // Unroll the exec command
    
    bool unroll = (0 == AZ::StringFunc::Find(str, "exec", 0, false, false));

    if (unroll)
    {
        bool oldDeferredExecution = m_deferredExecution;

        // Make sure that the unrolled commands are processed with deferred mode on
        m_deferredExecution = true;
        ExecuteStringInternal(str.c_str(), false, bSilentMode);

        // Restore to the previous setting
        m_deferredExecution = oldDeferredExecution;
    }
    else
    {
        m_deferredCommands.push_back(SDeferredCommand(str.c_str(), bSilentMode));
    }
}

// This method is used by the ConsoleRequestBus to allow executing of console commands
// This can be used from anywhere in code or via script since the bus is relected to the behavior context
void CXConsole::ExecuteConsoleCommand(const char* command)
{
    ExecuteString(command, true, true);
}

void CXConsole::ResetCVarsToDefaults()
{
    ConsoleVariablesMapItor It = m_mapVariables.begin();

    while (It != m_mapVariables.end())
    {
        It->second->Reset();
        ++It;
    }

}

void CXConsole::SplitCommands(const char* line, std::list<AZStd::string>& split)
{
    const char* start = line;
    AZStd::string working;

    while (true)
    {
        char ch = *line++;
        switch (ch)
        {
        case '\'':
        case '\"':
            while ((*line++ != ch) && *line)
            {
                ;
            }
            break;
        case '\n':
        case '\r':
        case ';':
        case '\0':
        {
            working.assign(start, line - 1);
            AZ::StringFunc::TrimWhiteSpace(working, true, true);

            if (!working.empty())
            {
                split.push_back(working);
            }
            start = line;

            if (ch == '\0')
            {
                return;
            }
        }
        break;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ExecuteStringInternal(const char* command, const bool bFromConsole, const bool bSilentMode)
{
    AzFramework::ConsoleNotificationBus::Broadcast(&AzFramework::ConsoleNotificationBus::Events::OnConsoleCommandExecuted, command);

    assert(command);
    assert(command[0] != '\\');           // caller should remove leading "\\"

    ///////////////////////////
    //Execute as string
    if (command[0] == '#' || command[0] == '@')
    {
        if (!con_restricted || !bFromConsole)            // in restricted mode we allow only VF_RESTRICTEDMODE CVars&CCmd
        {
            AddLine(command);

            if (m_pSystem->IsDevMode())
            {
                m_bDrawCursor = 0;
            }
            else
            {
                // Warning.
                // No Cheat warnings. ConsoleWarning("Console execution is cheat protected");
            }
            return;
        }
    }

    ConsoleCommandsMapItor itrCmd;
    ConsoleVariablesMapItor itrVar;

    std::list<AZStd::string> lineCommands;
    SplitCommands(command, lineCommands);

    AZStd::string sTemp;
    AZStd::string sCommand, sLineCommand;

    while (!lineCommands.empty())
    {
        AZStd::string::size_type nPos;

        {
            sTemp = lineCommands.front();
            sCommand = lineCommands.front();
            sLineCommand = sCommand;
            lineCommands.pop_front();

            if (!bSilentMode)
            {
                if (GetStatus())
                {
                    AddLine(sTemp.c_str());
                }
            }

            nPos = sTemp.find_first_of('=');

            if (nPos != AZStd::string::npos)
            {
                sCommand = sTemp.substr(0, nPos);
            }
            else if ((nPos = sTemp.find_first_of(' ')) != AZStd::string::npos)
            {
                sCommand = sTemp.substr(0, nPos);
            }
            else
            {
                sCommand = sTemp;
            }

            AZ::StringFunc::TrimWhiteSpace(sCommand, true, true);

            //////////////////////////////////////////
            // Search for CVars
            if (sCommand.length() > 1 && sCommand[0] == '?')
            {
                sTemp = sCommand.substr(1);
                FindVar(sTemp.c_str());
                continue;
            }
        }

        //////////////////////////////////////////
        //Check if is a command
        itrCmd = m_mapCommands.find(sCommand);
        if (itrCmd != m_mapCommands.end())
        {
            if (((itrCmd->second).m_nFlags & VF_RESTRICTEDMODE) || !con_restricted || !bFromConsole)           // in restricted mode we allow only VF_RESTRICTEDMODE CVars&CCmd
            {
                if (itrCmd->second.m_nFlags & VF_BLOCKFRAME)
                {
                    m_blockCounter++;
                }

                {
                    sTemp = sLineCommand;
                }
                ExecuteCommand((itrCmd->second), sTemp);

                continue;
            }
        }

        //////////////////////////////////////////
        //Check  if is a variable
        itrVar = m_mapVariables.find(sCommand.c_str());
        if (itrVar != m_mapVariables.end())
        {
            ICVar* pCVar = itrVar->second;

            if ((pCVar->GetFlags() & VF_RESTRICTEDMODE) || !con_restricted || !bFromConsole)           // in restricted mode we allow only VF_RESTRICTEDMODE CVars&CCmd
            {
                if (pCVar->GetFlags() & VF_BLOCKFRAME)
                {
                    m_blockCounter++;
                }

                if (nPos != AZStd::string::npos)
                {
                    sTemp = sTemp.substr(nPos + 1);     // remove the command from sTemp
                    AZ::StringFunc::StripEnds(sTemp, " \t\r\n\"\'");

                    if (sTemp == "?")
                    {
                        ICVar* v = itrVar->second;
                        DisplayHelp(v->GetHelp(), sCommand.c_str());
                        return;
                    }

                    if (!sTemp.empty() || (pCVar->GetType() == CVAR_STRING))
                    {
                        pCVar->Set(sTemp.c_str());
                    }
                }

                // the following line calls AddLine() indirectly
                if (!bSilentMode)
                {
                    DisplayVarValue(pCVar);
                }
                //ConsoleLogInputResponse("%s=%s",pCVar->GetName(),pCVar->GetString());
                continue;
            }
        }

        if (!bSilentMode)
        {
            ConsoleWarning("Unknown command: %s", sCommand.c_str());
        }
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ExecuteDeferredCommands()
{
    TDeferredCommandList::iterator it;

    if (m_waitFrames)
    {
        --m_waitFrames;
        return;
    }

    if (m_waitSeconds.GetValue())
    {
        if (m_waitSeconds > gEnv->pTimer->GetFrameStartTime())
        {
            return;
        }

        // Help to avoid overflow problems
        m_waitSeconds.SetValue(0);
    }

    const int blockCounter = m_blockCounter;

    // Signal the console that we executing a deferred command
    //m_deferredExecution = true;

    while (!m_deferredCommands.empty())
    {
        it = m_deferredCommands.begin();
        ExecuteStringInternal(it->command.c_str(), false, it->silentMode);
        m_deferredCommands.pop_front();

        // A blocker command was executed
        if (m_blockCounter != blockCounter)
        {
            break;
        }
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ExecuteCommand(CConsoleCommand& cmd, AZStd::string& str, bool bIgnoreDevMode)
{
    CryLog ("[CONSOLE] Executing console command '%s'", str.c_str());
    INDENT_LOG_DURING_SCOPE();

    std::vector<AZStd::string> args;
    size_t t;

    {
        t = 1;

        const char* start = str.c_str();
        const char* commandLine = start;
        while (char ch = *commandLine++)
        {
            switch (ch)
            {
            case '\'':
            case '\"':
            {
                while ((*commandLine++ != ch) && *commandLine)
                {
                    ;
                }
                args.push_back(AZStd::string(start + 1, commandLine - 1));
                start = commandLine;
                break;
            }
            case ' ':
                start = commandLine;
                break;
            default:
            {
                if ((*commandLine == ' ') || !*commandLine)
                {
                    args.push_back(AZStd::string(start, commandLine));
                    start = commandLine + 1;
                }
            }
            break;
            }
        }

        if (args.size() >= 2 && args[1] == "?")
        {
            DisplayHelp(cmd.m_sHelp.c_str(), cmd.m_sName.c_str());
            return;
        }

        if (((cmd.m_nFlags & (VF_CHEAT | VF_CHEAT_NOCHECK | VF_CHEAT_ALWAYS_CHECK)) != 0) && !(gEnv->IsEditor()))
        {
#if LOG_CVAR_INFRACTIONS
            gEnv->pLog->LogError("[CVARS]: [EXECUTE] command %s is marked [VF_CHEAT]", cmd.m_sName.c_str());
#if LOG_CVAR_INFRACTIONS_CALLSTACK
            gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
#endif // LOG_CVAR_INFRACTIONS
            if (!(gEnv->IsEditor()) && !(m_pSystem->IsDevMode()) && !bIgnoreDevMode)
            {
                return;
            }
        }
    }

    if (cmd.m_func)
    {
        // This is function command, execute it with a list of parameters.
        CConsoleCommandArgs cmdArgs(str, args);
        cmd.m_func(&cmdArgs);
        return;
    }

    AZStd::string buf;
    {
        // only do this for commands with script implementation
        for (;; )
        {
            t = str.find_first_of("\\", t);
            if (t == AZStd::string::npos)
            {
                break;
            }
            str.replace(t, 1, "\\\\", 2);
            t += 2;
        }

        for (t = 1;; )
        {
            t = str.find_first_of("\"", t);
            if (t == AZStd::string::npos)
            {
                break;
            }
            str.replace(t, 1, "\\\"", 2);
            t += 2;
        }

        buf = cmd.m_sCommand;

        size_t pp = buf.find("%%");
        if (pp != AZStd::string::npos)
        {
            AZStd::string list = "";
            for (unsigned int i = 1; i < args.size(); i++)
            {
                list += "\"" + args[i] + "\"";
                if (i < args.size() - 1)
                {
                    list += ",";
                }
            }
            buf.replace(pp, 2, list);
        }
        else if ((pp = buf.find("%line")) != AZStd::string::npos)
        {
            AZStd::string tmp = "\"" + str.substr(str.find(" ") + 1) + "\"";
            if (args.size() > 1)
            {
                buf.replace(pp, 5, tmp);
            }
            else
            {
                buf.replace(pp, 5, "");
            }
        }
        else
        {
            for (unsigned int i = 1; i <= args.size(); i++)
            {
                char pat[10];
                azsprintf(pat, "%%%d", i);
                size_t pos = buf.find(pat);
                if (pos == AZStd::string::npos)
                {
                    if (i != args.size())
                    {
                        ConsoleWarning("Too many arguments for: %s", cmd.m_sName.c_str());
                        return;
                    }
                }
                else
                {
                    if (i == args.size())
                    {
                        ConsoleWarning("Not enough arguments for: %s", cmd.m_sName.c_str());
                        return;
                    }
                    AZStd::string arg = "\"" + args[i] + "\"";
                    buf.replace(pos, strlen(pat), arg);
                }
            }
        }
    }

    m_bDrawCursor = 0;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::Exit(const char* szExitComments, ...)
{
    char sResultMessageText[1024] = "";

    if (szExitComments)
    {
        // make result string
        va_list     arglist;
        va_start(arglist, szExitComments);
        vsprintf_s(sResultMessageText, szExitComments, arglist);
        va_end(arglist);
    }
    else
    {
        azstrcpy(sResultMessageText, AZ_ARRAY_SIZE(sResultMessageText), "No comments from application");
    }

    CryFatalError("%s", sResultMessageText);
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::RegisterAutoComplete(const char* sVarOrCommand, IConsoleArgumentAutoComplete* pArgAutoComplete)
{
    m_mapArgumentAutoComplete[sVarOrCommand] = pArgAutoComplete;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::UnRegisterAutoComplete(const char* sVarOrCommand)
{
    ArgumentAutoCompleteMap::iterator it;
    it = m_mapArgumentAutoComplete.find(sVarOrCommand);
    if (it != m_mapArgumentAutoComplete.end())
    {
        m_mapArgumentAutoComplete.erase(it);
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ResetAutoCompletion()
{
    m_nTabCount = 0;
    m_sPrevTab = "";
}

//////////////////////////////////////////////////////////////////////////
const char* CXConsole::ProcessCompletion(const char* szInputBuffer)
{
    m_sInputBuffer = szInputBuffer;

    int offset = (szInputBuffer[0] == '\\' ? 1 : 0);        // legacy support

    if ((m_sPrevTab.size() > strlen(szInputBuffer + offset)) || _strnicmp(m_sPrevTab.c_str(), (szInputBuffer + offset), m_sPrevTab.size()))
    {
        m_nTabCount = 0;
        m_sPrevTab = "";
    }

    if (m_sInputBuffer.empty())
    {
        return (char*)m_sInputBuffer.c_str();
    }

    int nMatch = 0;
    ConsoleCommandsMapItor itrCmds;
    ConsoleVariablesMapItor itrVars;
    bool showlist = !m_nTabCount && m_sPrevTab == "";

    if (m_nTabCount == 0)
    {
        if (m_sInputBuffer.size() > 0)
        {
            if (m_sInputBuffer[0] == '\\')
            {
                m_sPrevTab = &m_sInputBuffer.c_str()[1];      // legacy support
            }
            else
            {
                m_sPrevTab = m_sInputBuffer;
            }
        }

        else
        {
            m_sPrevTab = "";
        }
    }
    //try to search in command list
    bool bArgumentAutoComplete = false;
    std::vector<AZStd::string> matches;

    if (m_sPrevTab.find(' ') != AZStd::string::npos)
    {
        bool bProcessAutoCompl = true;

        // Find command.
        AZStd::string sVar = m_sPrevTab.substr(0, m_sPrevTab.find(' '));
        ICVar* pCVar = GetCVar(sVar.c_str());
        if (pCVar)
        {
            if (!(pCVar->GetFlags() & VF_RESTRICTEDMODE) && con_restricted)            // in restricted mode we allow only VF_RESTRICTEDMODE CVars&CCmd
            {
                bProcessAutoCompl = false;
            }
        }

        ConsoleCommandsMap::iterator it = m_mapCommands.find(sVar);
        if (it != m_mapCommands.end())
        {
            CConsoleCommand& ccmd = it->second;

            if (!(ccmd.m_nFlags & VF_RESTRICTEDMODE) && con_restricted)            // in restricted mode we allow only VF_RESTRICTEDMODE CVars&CCmd
            {
                bProcessAutoCompl = false;
            }
        }

        if (bProcessAutoCompl)
        {
            IConsoleArgumentAutoComplete* pArgumentAutoComplete = stl::find_in_map(m_mapArgumentAutoComplete, sVar, 0);
            if (pArgumentAutoComplete)
            {
                int nMatches = pArgumentAutoComplete->GetCount();
                for (int i = 0; i < nMatches; i++)
                {
                    AZStd::string cmd = AZStd::string(sVar) + " " + pArgumentAutoComplete->GetValue(i);
                    if (_strnicmp(m_sPrevTab.c_str(), cmd.c_str(), m_sPrevTab.length()) == 0)
                    {
                        {
                            bArgumentAutoComplete = true;
                            matches.push_back(cmd);
                        }
                    }
                }
            }
        }
    }

    if (!bArgumentAutoComplete)
    {
        itrCmds = m_mapCommands.begin();
        while (itrCmds != m_mapCommands.end())
        {
            CConsoleCommand& cmd = itrCmds->second;

            if ((cmd.m_nFlags & VF_RESTRICTEDMODE) || !con_restricted)         // in restricted mode we allow only VF_RESTRICTEDMODE CVars&CCmd
            {
                if (_strnicmp(m_sPrevTab.c_str(), itrCmds->first.c_str(), m_sPrevTab.length()) == 0)
                {
                    {
                        matches.push_back((char* const)itrCmds->first.c_str());
                    }
                }
            }
            ++itrCmds;
        }

        // try to search in console variables

        itrVars = m_mapVariables.begin();
        while (itrVars != m_mapVariables.end())
        {
            ICVar* pVar = itrVars->second;

            if ((pVar->GetFlags() & VF_RESTRICTEDMODE) || !con_restricted)         // in restricted mode we allow only VF_RESTRICTEDMODE CVars&CCmd
            {//if(itrVars->first.compare(0,m_sPrevTab.length(),m_sPrevTab)==0)
                if (_strnicmp(m_sPrevTab.c_str(), itrVars->first, m_sPrevTab.length()) == 0)
                {
                    {
                        matches.push_back((char* const)itrVars->first);
                    }
                }
            }
            ++itrVars;
        }
    }

    if (!matches.empty())
    {
        std::sort(matches.begin(), matches.end());       // to sort commands with variables
    }
    if (showlist && !matches.empty())
    {
        ConsoleLogInput(" ");       // empty line before auto completion

        for (std::vector<AZStd::string>::iterator i = matches.begin(); i != matches.end(); ++i)
        {
            // List matching variables
            const char* sVar = i->c_str();
            ICVar* pVar = GetCVar(sVar);

            if (pVar)
            {
                DisplayVarValue(pVar);
            }
            else
            {
                ConsoleLogInputResponse("    $3%s $6(Command)", sVar);
            }
        }
    }

    for (std::vector<AZStd::string>::iterator i = matches.begin(); i != matches.end(); ++i)
    {
        if (m_nTabCount <= nMatch)
        {
            m_sInputBuffer = *i;
            m_sInputBuffer += " ";
            m_nTabCount = nMatch + 1;
            return (char*)m_sInputBuffer.c_str();
        }
        nMatch++;
    }

    if (m_nTabCount > 0)
    {
        m_nTabCount = 0;
        m_sInputBuffer = m_sPrevTab;
        m_sInputBuffer = ProcessCompletion(m_sInputBuffer.c_str());
    }

    return (char*)m_sInputBuffer.c_str();
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::DisplayVarValue(ICVar* pVar)
{
    if (!pVar)
    {
        return;
    }

    const char* sFlagsString = GetFlagsString(pVar->GetFlags());

    AZStd::string sValue = (pVar->GetFlags() & VF_INVISIBLE) ? "" : pVar->GetString();
    AZStd::string sVar = pVar->GetName();

    char szRealState[40] = "";

    if (pVar->GetType() == CVAR_INT)
    {
        int iRealState = pVar->GetRealIVal();

        if (iRealState != pVar->GetIVal())
        {
            if (iRealState == -1)
            {
                azstrcpy(szRealState, AZ_ARRAY_SIZE(szRealState), " RealState=Custom");
            }
            else
            {
                sprintf_s(szRealState, " RealState=%d", iRealState);
            }
        }
    }

    if (pVar->GetFlags() & VF_BITFIELD)
    {
        uint64 val64 = pVar->GetI64Val();
        uint64 alphaBits = val64 & ~63LL;
        uint32 nonAlphaBits = val64 & 63;

        if (alphaBits != 0)
        {
            // the bottom 6 bits can't be set by char entry, so show them separately
            char alphaChars[65];    // 1 char per bit + '\0'
            BitsAlpha64(alphaBits, alphaChars);
            sValue += " (";
            if (nonAlphaBits != 0)
            {
                char nonAlphaChars[3] = { 0 };  // 1..63 + '\0'
                azitoa(nonAlphaBits, nonAlphaChars, AZ_ARRAY_SIZE(nonAlphaChars), 10);
                sValue += nonAlphaChars;
                sValue += ", ";
            }
            sValue += alphaChars;
            sValue += ")";
        }
    }

    if (gEnv->IsEditor())
    {
        ConsoleLogInputResponse("%s=%s [ %s ]%s", sVar.c_str(), sValue.c_str(), sFlagsString, szRealState);
    }
    else
    {
        ConsoleLogInputResponse("    $3%s = $6%s $5[%s]$4%s", sVar.c_str(), sValue.c_str(), sFlagsString, szRealState);
    }
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::IsOpened()
{
    return m_nScrollPos == m_nTempScrollMax;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::PrintLine(const char* s)
{
    AddLine(s);
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::PrintLinePlus(const char* s)
{
    AddLinePlus(s);
}

static const char* FindNextEndOfLineCharacter(const char* str, size_t length)
{
    size_t index = 0;
    while (index < length)
    {
        if ((str[index] == '\r') || (str[index] == '\n'))
        {
            return (str + index);
        }

        index++;
    }

    return nullptr;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::AddLine(const char* inputStr)
{
    if ((inputStr == nullptr) || (inputStr[0] == 0))
    {
        return;
    }

    size_t totalLen = strlen(inputStr);

    // strip trailing \n or \r.
    while ((totalLen > 0) && (inputStr[totalLen - 1] == '\n') || (inputStr[totalLen - 1] == '\r'))
    {
        totalLen--;
    }

    // split out each line in a memory efficient way
    size_t remainingLength = totalLen;
    const char* lastLine = inputStr;
    const char* cursor = FindNextEndOfLineCharacter(inputStr, remainingLength);
    while (cursor != nullptr)
    {
        size_t subStrLength = (cursor - lastLine);

        PostLine(lastLine, subStrLength);

        // bump us up to the cursor + 1 to move past the end of line character
        remainingLength = remainingLength - (subStrLength + 1);
        lastLine = (cursor + 1);

        // Find the next non-end of line character
        while ((remainingLength > 0) && ((*lastLine == '\n') || (*lastLine == '\r')))
        {
            remainingLength--;
            lastLine++;
        }

        cursor = FindNextEndOfLineCharacter(lastLine, remainingLength);
    }

    // check for leftover
    if (remainingLength > 0)
    {
        PostLine(lastLine, remainingLength);
    }
}

void CXConsole::PostLine(const char* lineOfText, size_t len)
{
    AZStd::string line = AZStd::string(lineOfText, len);
    m_dqConsoleBuffer.push_back(line);

    int nBufferSize = con_line_buffer_size;

    while (((int)(m_dqConsoleBuffer.size())) > nBufferSize)
    {
        m_dqConsoleBuffer.pop_front();
    }

    // tell everyone who is interested (e.g. dedicated server printout)
    {
        std::vector<IOutputPrintSink*>::iterator it;

        for (it = m_OutputSinks.begin(); it != m_OutputSinks.end(); ++it)
        {
            (*it)->Print(line.c_str());
        }
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ResetProgressBar(int)
{
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::TickProgressBar()
{
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::SetLoadingImage(const char*)
{
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::AddOutputPrintSink(IOutputPrintSink* inpSink)
{
    assert(inpSink);
    m_OutputSinks.push_back(inpSink);
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::RemoveOutputPrintSink(IOutputPrintSink* inpSink)
{
    assert(inpSink);

    int nCount = static_cast<int>(m_OutputSinks.size());

    for (int i = 0; i < nCount; i++)
    {
        if (m_OutputSinks[i] == inpSink)
        {
            if (nCount <= 1)
            {
                m_OutputSinks.clear();
            }
            else
            {
                m_OutputSinks[i] = m_OutputSinks.back();
                m_OutputSinks.pop_back();
            }
            return;
        }
    }

    assert(0);
}


//////////////////////////////////////////////////////////////////////////
void CXConsole::AddLinePlus(const char* inputStr)
{
    AZStd::string str, tmpStr;

    {
        if (!m_dqConsoleBuffer.size())
        {
            return;
        }

        str = inputStr;

        // strip trailing \n or \r.
        if (!str.empty() && (str[str.size() - 1] == '\n' || str[str.size() - 1] == '\r'))
        {
            str.resize(str.size() - 1);
        }

        AZStd::string::size_type nPos;
        while ((nPos = str.find('\n')) != AZStd::string::npos)
        {
            str.replace(nPos, 1, 1, ' ');
        }

        while ((nPos = str.find('\r')) != AZStd::string::npos)
        {
            str.replace(nPos, 1, 1, ' ');
        }

        tmpStr = m_dqConsoleBuffer.back();// += str;

        m_dqConsoleBuffer.pop_back();

        if (tmpStr.size() < 256)
        {
            m_dqConsoleBuffer.push_back(tmpStr + str);
        }
        else
        {
            m_dqConsoleBuffer.push_back(tmpStr);
        }
    }

    // tell everyone who is interested (e.g. dedicated server printout)
    {
        std::vector<IOutputPrintSink*>::iterator it;

        for (it = m_OutputSinks.begin(); it != m_OutputSinks.end(); ++it)
        {
            (*it)->Print((tmpStr + str).c_str());
        }
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::AddInputUTF8(const AZStd::string& textUTF8)
{
    // Ignore control characters like backspace and tab
    AZStd::string textUTF8ToInsert;
    for (int i = 0; i < textUTF8.length(); ++i)
    {
        const char charToInsert = textUTF8.at(i);
        if (!AZStd::is_cntrl(charToInsert))
        {
            textUTF8ToInsert += charToInsert;
        }
    }

    const char* utf8_buf = textUTF8ToInsert.c_str();

    if (m_nCursorPos < (int)(m_sInputBuffer.length()))
    {
        m_sInputBuffer.insert(m_nCursorPos, utf8_buf);
    }
    else
    {
        m_sInputBuffer = m_sInputBuffer + utf8_buf;
    }
    m_nCursorPos += strlen(utf8_buf);
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ExecuteInputBuffer()
{
    AZStd::string sTemp = m_sInputBuffer;
    if (m_sInputBuffer.empty())
    {
        return;
    }
    m_sInputBuffer = "";

    AddCommandToHistory(sTemp.c_str());

    {
        ExecuteStringInternal(sTemp.c_str(), true);     // from console
    }

    m_nCursorPos = 0;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::RemoveInputChar(bool bBackSpace)
{
    if (m_sInputBuffer.empty())
    {
        return;
    }

    if (bBackSpace)
    {
        if (m_nCursorPos > 0)
        {
            const char* const pBase = m_sInputBuffer.c_str();
            const char* pCursor = pBase + m_nCursorPos;
            const char* const pEnd = pCursor;
            pCursor -= Utf8::Internal::sequence_length(pCursor); // Remove one UCS code-point, doesn't account for combining diacritics
            size_t length = pEnd - pCursor;
            m_sInputBuffer.erase(pCursor - pBase, length);
            m_nCursorPos -= length;
        }
    }
    else
    {
        if (m_nCursorPos < (int)(m_sInputBuffer.length()))
        {
            const char* const pBase = m_sInputBuffer.c_str();
            const char* pCursor = pBase + m_nCursorPos;
            const char* const pBegin = pCursor;
            pCursor -= Utf8::Internal::sequence_length(pCursor); // Remove one UCS code-point, doesn't account for combining diacritics
            size_t length = pCursor - pBegin;
            m_sInputBuffer.erase(pBegin - pBase, length);
        }
    }
}


//////////////////////////////////////////////////////////////////////////
void CXConsole::AddCommandToHistory(const char* szCommand)
{
    assert(szCommand);

    m_nHistoryPos = -1;

    if (!m_dqHistory.empty())
    {
        // add only if the command is != than the last
        if (m_dqHistory.front() != szCommand)
        {
            m_dqHistory.push_front(szCommand);
        }
    }
    else
    {
        m_dqHistory.push_front(szCommand);
    }

    while (m_dqHistory.size() > MAX_HISTORY_ENTRIES)
    {
        m_dqHistory.pop_back();
    }
}


//////////////////////////////////////////////////////////////////////////
void CXConsole::Copy()
{
#ifdef AZ_PLATFORM_WINDOWS
    if (m_sInputBuffer.empty())
    {
        return;
    }

    if (!OpenClipboard(NULL))
    {
        return;
    }

    size_t stringLength = m_sInputBuffer.length();
    size_t requiredBufferSize = stringLength + 1; // for the null

    if (HGLOBAL hGlobal = GlobalAlloc(GHND, requiredBufferSize))
    {
        if (LPVOID  pGlobal = GlobalLock(hGlobal))
        {
            azstrcpy((char*)pGlobal, requiredBufferSize, m_sInputBuffer.c_str());
            GlobalUnlock(hGlobal);
            EmptyClipboard();
            SetClipboardData(CF_TEXT, hGlobal);
            CloseClipboard();
        }
    }

    return;
#endif //AZ_PLATFORM_WINDOWS
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::Paste()
{
#if defined(AZ_PLATFORM_WINDOWS)
    if (OpenClipboard(NULL) != 0)
    {
        AZStd::string data;
        const HANDLE wideData = GetClipboardData(CF_UNICODETEXT);
        if (wideData)
        {
            const LPCWSTR pWideData = (LPCWSTR)GlobalLock(wideData);
            if (pWideData)
            {
                AZStd::to_string(data, pWideData);
                GlobalUnlock(wideData);
            }
        }
        CloseClipboard();

        Utf8::Unchecked::octet_iterator end(data.end());
        for (Utf8::Unchecked::octet_iterator it(data.begin()); it != end; ++it)
        {
            const wchar_t cp = static_cast<wchar_t>(*it);
            if (cp != '\r')
            {
                // Convert UCS code-point into UTF-8 string
                AZStd::fixed_string<5> utf8_buf = {0};
                AZStd::to_string(utf8_buf.data(), 5, { &cp, 1 });
                AddInputUTF8(utf8_buf.c_str());
            }
        }
    }
#endif //AZ_PLATFORM_WINDOWS
}


//////////////////////////////////////////////////////////////////////////
int CXConsole::GetNumVars()
{
    return static_cast<int>(m_mapVariables.size());
}

//////////////////////////////////////////////////////////////////////////
int CXConsole::GetNumVisibleVars()
{
    int numVars = 0;

    for (auto& v : m_mapVariables)
    {
        if ((v.second->GetFlags() & VF_INVISIBLE) == 0)
            ++numVars;
    }

    return numVars;
}


//////////////////////////////////////////////////////////////////////////
bool CXConsole::IsHashCalculated()
{
    return m_bCheatHashDirty == false;
}

//////////////////////////////////////////////////////////////////////////
int CXConsole::GetNumCheatVars()
{
    return static_cast<int>(m_randomCheckedVariables.size());
}

//////////////////////////////////////////////////////////////////////////
uint64 CXConsole::GetCheatVarHash()
{
    return m_nCheatHash;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::SetCheatVarHashRange(size_t firstVar, size_t lastVar)
{
    // check inputs are sane
#if !defined(NDEBUG)
    size_t numVars = GetNumCheatVars();
    assert(firstVar < numVars && lastVar < numVars && lastVar >= firstVar);
#endif

#if defined(DEFENCE_CVAR_HASH_LOGGING)
    if (m_bCheatHashDirty)
    {
        CryLog("HASHING: WARNING - trying to set up new cvar hash range while existing hash still calculating!");
    }
#endif

    m_nCheatHashRangeFirst = firstVar;
    m_nCheatHashRangeLast = lastVar;
    m_bCheatHashDirty = true;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::CalcCheatVarHash()
{
    if (!m_bCheatHashDirty)
    {
        return;
    }

    CCrc32 runningNameCrc32;
    CCrc32 runningNameValueCrc32;

    AddCVarsToHash(m_randomCheckedVariables.begin() + m_nCheatHashRangeFirst, m_randomCheckedVariables.begin() + m_nCheatHashRangeLast, runningNameCrc32, runningNameValueCrc32);
    AddCVarsToHash(m_alwaysCheckedVariables.begin(), m_alwaysCheckedVariables.end() - 1, runningNameCrc32, runningNameValueCrc32);

    // store hash
    m_nCheatHash = (((uint64)runningNameCrc32.Get()) << 32) | runningNameValueCrc32.Get();
    m_bCheatHashDirty = false;

#if defined(DEFENCE_CVAR_HASH_LOGGING)
    if (!gEnv->IsDedicated())
    {
        CryLog("HASHING: Range %d->%d = %llx(%x,%x), max cvars = %d", m_nCheatHashRangeFirst, m_nCheatHashRangeLast,
            m_nCheatHash, runningNameCrc32.Get(), runningNameValueCrc32.Get(),
            GetNumCheatVars());
        PrintCheatVars(true);
    }
#endif
}

void CXConsole::AddCVarsToHash(ConsoleVariablesVector::const_iterator begin, ConsoleVariablesVector::const_iterator end, CCrc32& runningNameCrc32, CCrc32& runningNameValueCrc32)
{
    for (ConsoleVariablesVector::const_iterator it = begin; it <= end; ++it)
    {
        // add name & variable to string. We add both since adding only the value could cause
        // many collisions with variables all having value 0 or all 1.
        AZStd::string hashStr = it->first;

        runningNameCrc32.Add(hashStr.c_str(), hashStr.length());
        hashStr += it->second->GetDataProbeString();
        runningNameValueCrc32.Add(hashStr.c_str(), hashStr.length());
    }
}

void CXConsole::CmdDumpAllAnticheatVars([[maybe_unused]] IConsoleCmdArgs* pArgs)
{
#if defined(DEFENCE_CVAR_HASH_LOGGING)
    CXConsole* pConsole = (CXConsole*)gEnv->pConsole;

    if (pConsole->IsHashCalculated())
    {
        CryLog("HASHING: Displaying Full Anticheat Cvar list:");
        pConsole->PrintCheatVars(false);
    }
    else
    {
        CryLogAlways("DumpAllAnticheatVars - cannot complete, cheat vars are in a state of flux, please retry.");
    }
#endif
}

void CXConsole::CmdDumpLastHashedAnticheatVars([[maybe_unused]] IConsoleCmdArgs* pArgs)
{
#if defined(DEFENCE_CVAR_HASH_LOGGING)
    CXConsole* pConsole = (CXConsole*)gEnv->pConsole;

    if (pConsole->IsHashCalculated())
    {
        CryLog("HASHING: Displaying Last Hashed Anticheat Cvar list:");
        pConsole->PrintCheatVars(true);
    }
    else
    {
        CryLogAlways("DumpLastHashedAnticheatVars - cannot complete, cheat vars are in a state of flux, please retry.");
    }
#endif
}

void CXConsole::PrintCheatVars([[maybe_unused]] bool bUseLastHashRange)
{
#if defined(DEFENCE_CVAR_HASH_LOGGING)
    if (m_bCheatHashDirty)
    {
        return;
    }

    size_t i = 0;
    char floatFormatBuf[64];

    size_t nStart = 0;
    size_t nEnd = m_mapVariables.size();

    if (bUseLastHashRange)
    {
        nStart = m_nCheatHashRangeFirst;
        nEnd = m_nCheatHashRangeLast;
    }

    // iterate over all const cvars in our range
    // then hash the string.
    CryLog("VF_CHEAT & ~VF_CHEAT_NOCHECK list:");

    ConsoleVariablesMap::const_iterator it, end = m_mapVariables.end();
    for (it = m_mapVariables.begin(); it != end; ++it)
    {
        // only count cheat cvars
        if ((it->second->GetFlags() & VF_CHEAT) == 0 ||
            (it->second->GetFlags() & VF_CHEAT_NOCHECK) != 0)
        {
            continue;
        }

        // count up
        i++;

        // if we haven't reached the first var, or have passed the last var, break out
        if (i - 1 < nStart)
        {
            continue;
        }
        if (i - 1 > nEnd)
        {
            break;
        }

        // add name & variable to string. We add both since adding only the value could cause
        // many collisions with variables all having value 0 or all 1.
        string hashStr = it->first;
        if (it->second->GetType() == CVAR_FLOAT)
        {
            sprintf(floatFormatBuf, "%.1g", it->second->GetFVal());
            hashStr += floatFormatBuf;
        }
        else
        {
            hashStr += it->second->GetString();
        }

        CryLog("%s", hashStr.c_str());
    }

    // iterate over any must-check variables
    CryLog("VF_CHEAT_ALWAYS_CHECK list:");

    for (it = m_mapVariables.begin(); it != end; ++it)
    {
        // only count cheat cvars
        if ((it->second->GetFlags() & VF_CHEAT_ALWAYS_CHECK) == 0)
        {
            continue;
        }

        // add name & variable to string. We add both since adding only the value could cause
        // many collisions with variables all having value 0 or all 1.
        string hashStr = it->first;
        hashStr += it->second->GetString();

        CryLog("%s", hashStr.c_str());
    }
#endif
}

char* CXConsole::GetCheatVarAt(uint32 nOffset)
{
    if (m_bCheatHashDirty)
    {
        return NULL;
    }

    size_t i = 0;
    size_t nStart = nOffset;

    // iterate over all const cvars in our range
    // then hash the string.
    ConsoleVariablesMap::const_iterator it, end = m_mapVariables.end();
    for (it = m_mapVariables.begin(); it != end; ++it)
    {
        // only count cheat cvars
        if ((it->second->GetFlags() & VF_CHEAT) == 0 ||
            (it->second->GetFlags() & VF_CHEAT_NOCHECK) != 0)
        {
            continue;
        }

        // count up
        i++;

        // if we haven't reached the first var continue
        if (i - 1 < nStart)
        {
            continue;
        }

        return (char*)it->first;
    }

    return NULL;
}


//////////////////////////////////////////////////////////////////////////
size_t CXConsole::GetSortedVars(AZStd::vector<AZStd::string_view>& pszArray, const char* szPrefix)
{
    // This method used to insert instead of push_back, so we need to clear first
    pszArray.clear();

    size_t iPrefixLen = szPrefix ? strlen(szPrefix) : 0;

    // variables
    {
        ConsoleVariablesMap::const_iterator it, end = m_mapVariables.end();
        for (it = m_mapVariables.begin(); it != end; ++it)
        {
            if (szPrefix)
            {
                if (_strnicmp(it->first, szPrefix, iPrefixLen) != 0)
                {
                    continue;
                }
            }

            if (it->second->GetFlags() & VF_INVISIBLE)
            {
                continue;
            }

            pszArray.push_back(it->first);
        }
    }

    // commands
    {
        ConsoleCommandsMap::iterator it, end = m_mapCommands.end();
        for (it = m_mapCommands.begin(); it != end; ++it)
        {
            if (szPrefix)
            {
                if (_strnicmp(it->first.c_str(), szPrefix, iPrefixLen) != 0)
                {
                    continue;
                }
            }

            if (it->second.m_nFlags & VF_INVISIBLE)
            {
                continue;
            }

            pszArray.push_back(it->first.c_str());
        }
    }

    std::sort(pszArray.begin(), pszArray.end());
    return pszArray.size();
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::FindVar(const char* substr)
{
    AZStd::vector<AZStd::string_view> cmds;
    size_t cmdCount = GetSortedVars(cmds);

    for (size_t i = 0; i < cmdCount; i++)
    {
        if (AZ::StringFunc::Find(cmds[i], substr) != AZStd::string::npos)
        {
            ICVar* pCvar = gEnv->pConsole->GetCVar(cmds[i].data());
            if (pCvar)
            {
                DisplayVarValue(pCvar);
            }
            else
            {
                ConsoleLogInputResponse("    $3%.*s $6(Command)", aznumeric_cast<int>(cmds[i].size()), cmds[i].data());
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////
const char* CXConsole::AutoComplete(const char* substr)
{
    // following code can be optimized

    AZStd::vector<AZStd::string_view> cmds;
    size_t cmdCount = GetSortedVars(cmds);

    size_t substrLen = substr ? strlen(substr) : 0;

    // If substring is empty return first command.
    if (substrLen == 0 && cmdCount > 0)
    {
        return cmds[0].data();
    }

    // find next
    for (size_t i = 0; i < cmdCount; i++)
    {
        const char* szCmd = cmds[i].data();
        size_t cmdlen = cmds[i].size();
        if (cmdlen >= substrLen && memcmp(szCmd, substr, substrLen) == 0)
        {
            if (substrLen == cmdlen)
            {
                i++;
                if (i < cmdCount)
                {
                    return cmds[i].data();
                }
                return cmds[i - 1].data();
            }
            return cmds[i].data();
        }
    }

    // then first matching case insensitive
    for (size_t i = 0; i < cmdCount; i++)
    {
        const char* szCmd = cmds[i].data();

        size_t cmdlen = cmds[i].size();
        if (cmdlen >= substrLen && azstrnicmp(szCmd, substr, substrLen) == 0)
        {
            if (substrLen == cmdlen)
            {
                i++;
                if (i < cmdCount)
                {
                    return cmds[i].data();
                }
                return cmds[i - 1].data();
            }
            return cmds[i].data();
        }
    }

    // Not found.
    return "";
}

void CXConsole::SetInputLine(const char* szLine)
{
    assert(szLine);

    m_sInputBuffer = szLine;
    m_nCursorPos = m_sInputBuffer.size();
}



//////////////////////////////////////////////////////////////////////////
const char* CXConsole::AutoCompletePrev(const char* substr)
{
    AZStd::vector<AZStd::string_view> cmds;
    GetSortedVars(cmds);

    // If substring is empty return last command.
    if (strlen(substr) == 0 && !cmds.empty())
    {
        return cmds.back().data();
    }

    for (const AZStd::string_view& cmd : cmds)
    {
        if (azstricmp(substr, cmd.data()) == 0)
        {
            return cmd.data();
        }
    }
    return AutoComplete(substr);
}

//////////////////////////////////////////////////////////////////////////
inline size_t sizeOf (const AZStd::string& str)
{
    return str.capacity() + 1;
}

//////////////////////////////////////////////////////////////////////////
inline size_t sizeOf (const char* sz)
{
    return sz ? strlen(sz) + 1 : 0;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::GetMemoryUsage (class ICrySizer* pSizer) const
{
    pSizer->AddObject(this, sizeof(*this));
    pSizer->AddObject(m_sInputBuffer);
    pSizer->AddObject(m_sPrevTab);
    pSizer->AddObject(m_dqConsoleBuffer);
    pSizer->AddObject(m_dqHistory);
    pSizer->AddObject(m_mapCommands);
    pSizer->AddObject(m_mapBinds);
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ConsoleLogInputResponse(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    gEnv->pLog->LogV(ILog::eInputResponse, format, args);
    va_end(args);
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ConsoleLogInput(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    gEnv->pLog->LogV(ILog::eInput, format, args);
    va_end(args);
}


//////////////////////////////////////////////////////////////////////////
void CXConsole::ConsoleWarning(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    gEnv->pLog->LogV(ILog::eWarningAlways, format, args);
    va_end(args);
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::OnBeforeVarChange(ICVar* pVar, const char* sNewValue)
{
    bool isConst = pVar->IsConstCVar();
    bool isCheat = ((pVar->GetFlags() & (VF_CHEAT | VF_CHEAT_NOCHECK | VF_CHEAT_ALWAYS_CHECK)) != 0);
    bool isReadOnly = ((pVar->GetFlags() & VF_READONLY) != 0);
    bool isDeprecated = ((pVar->GetFlags() & VF_DEPRECATED) != 0);

    if (
#if CVAR_GROUPS_ARE_PRIVILEGED
        !m_bIsProcessingGroup &&
#endif // !CVAR_GROUPS_ARE_PRIVILEGED
        (isConst || isCheat || isReadOnly || isDeprecated))
    {
        bool allowChange = !isDeprecated && ((gEnv->pSystem->IsDevMode()) || (gEnv->IsEditor()));
        if (!(gEnv->IsEditor()) || isDeprecated)
        {
#if LOG_CVAR_INFRACTIONS
            LogChangeMessage(pVar->GetName(), isConst, isCheat,
                isReadOnly, isDeprecated, pVar->GetString(), sNewValue, m_bIsProcessingGroup, allowChange);
#if LOG_CVAR_INFRACTIONS_CALLSTACK
            gEnv->pSystem->debug_LogCallStack();
#endif // LOG_CVAR_INFRACTIONS_CALLSTACK
#endif // LOG_CVAR_INFRACTIONS
        }

        if (!allowChange && !ALLOW_CONST_CVAR_MODIFICATIONS)
        {
            return false;
        }
    }

    if (!m_consoleVarSinks.empty())
    {
        ConsoleVarSinks::iterator it, next;
        for (it = m_consoleVarSinks.begin(); it != m_consoleVarSinks.end(); it = next)
        {
            next = it;
            next++;
            if (!(*it)->OnBeforeVarChange(pVar, sNewValue))
            {
                return false;
            }
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::OnAfterVarChange(ICVar* pVar)
{
    if (!m_consoleVarSinks.empty())
    {
        ConsoleVarSinks::iterator it, next;
        for (it = m_consoleVarSinks.begin(); it != m_consoleVarSinks.end(); it = next)
        {
            next = it;
            next++;
            (*it)->OnAfterVarChange(pVar);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole_ExecuteCommandTrampoline(IConsoleCmdArgs* args)
{
    if (gEnv && gEnv->pSystem && gEnv->pSystem->GetIConsole())
    {
        CXConsole* pConsole = static_cast<CXConsole*>(gEnv->pSystem->GetIConsole());
        pConsole->ExecuteRegisteredCommand(args);
    }
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::ExecuteRegisteredCommand(IConsoleCmdArgs* args)
{
    if (args->GetArgCount() == 0)
    {
        AZ_Error("console", false, "Invalid number of args sent");
        return;
    }

    const char* commandIdentifier = args->GetArg(0);
    auto itCommandEntry = m_commandRegistrationMap.find(commandIdentifier);
    if (itCommandEntry == m_commandRegistrationMap.end())
    {
        AZ_Error("console", false, "Command %s not found in the command registry", commandIdentifier);
        return;
    }

    AZStd::vector<AZStd::string_view> input;
    input.reserve(args->GetArgCount());
    for (int i = 0; i < args->GetArgCount(); ++i)
    {
        input.push_back(args->GetArg(i));
    }

    CommandRegistrationEntry& entry = itCommandEntry->second;
    const AzFramework::CommandResult output = entry.m_callback(input);
    if (output != AzFramework::CommandResult::Success)
    {
        if (output == AzFramework::CommandResult::ErrorWrongNumberOfArguments)
        {
            AZ_Warning("console", false, "Command does not have the right number of arguments (send = %d)", input.size());
        }
        else
        {
            AZ_Warning("console", false, "Command returned a generic error");
        }
    }
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::RegisterCommand(AZStd::string_view identifier, AZStd::string_view helpText, AZ::u32 commandFlags, AzFramework::CommandFunction callback)
{
    if (identifier.empty())
    {
        AZ_Error("console", false, "RegisterCommand() requires a valid identifier");
        return false;
    }

    if (!callback)
    {
        AZ_Error("console", false, "RegisterCommand() requires a valid callback");
        return false;
    }

    if (m_commandRegistrationMap.find(identifier) != m_commandRegistrationMap.end())
    {
        AZ_Warning("console", false, "Command '%.*s' already found in the command registry.", static_cast<int>(identifier.size()), identifier.data());
        return false;
    }

    // command flags should match "enum EVarFlags" values
    const int flags = static_cast<int>(commandFlags);

    CommandRegistrationEntry entry;
    entry.m_callback = callback;
    entry.m_id = identifier;
    if (!helpText.empty())
    {
        entry.m_helpText = helpText;
    }

    if (!AddCommand(entry.m_id.c_str(), CXConsole_ExecuteCommandTrampoline, flags, entry.m_helpText.empty() ? nullptr : entry.m_helpText.c_str()))
    {
        AZ_Warning("console", false, "Command %s already found in the command registry.", entry.m_id.c_str());
        return false;
    }

    m_commandRegistrationMap.insert(AZStd::make_pair(entry.m_id, entry));
    return true;
}

//////////////////////////////////////////////////////////////////////////
bool CXConsole::UnregisterCommand(AZStd::string_view identifier)
{
    if (m_commandRegistrationMap.erase(identifier) == 1)
    {
        RemoveCommand(identifier.data());
        return true;
    }
    return false;
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::AddConsoleVarSink(IConsoleVarSink* pSink)
{
    m_consoleVarSinks.push_back(pSink);
}

//////////////////////////////////////////////////////////////////////////
void CXConsole::RemoveConsoleVarSink(IConsoleVarSink* pSink)
{
    m_consoleVarSinks.remove(pSink);
}
