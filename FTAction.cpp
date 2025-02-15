#include "FTAction.h"
#include "UserConfig.h"
#include "MenuNavigation.h"
#include "Menu.h"
#include "globals.hpp"
#include <algorithm>
#include <cstdio>
#include "ConfigLoad.h"
#include "System.h"
static const char *module = "FTAction";

using namespace std;

static char printBuffer[201] = {0};

namespace FreeTouchDeck
{
    const char *splitterFormat = "%[^.:,]%*s";
    const char *separatorFormat = "%[ .:,]";
    SemaphoreHandle_t xQueueSemaphore = xSemaphoreCreateMutex();
    std::queue<FTAction *> Queue;
    std::queue<FTAction *> ScreenQueue;
    std::string emptyString;
    const char *unknown = "Unknown";
    const char *FTAction::JsonLabelType = "type";
    const char *FTAction::JsonLabelValue = "value";
    const char *FTAction::JsonLabelSymbol = "symbol";
    FTAction FTAction::rebootSystem;

    FTAction::FTAction()
    {
        Type = ActionTypes::NONE;
    }
    const char *enum_to_string(ActionTypes type)
    {
        switch (type)
        {
            ENUM_TO_STRING_HELPER(ActionTypes, NONE);
            ENUM_TO_STRING_HELPER(ActionTypes, KEYBOARD);
            ENUM_TO_STRING_HELPER(ActionTypes, LOCAL);
        default:
            return unknown;
        }
    }

#define KEY_TO_MAP(k)         \
    {                         \
        QUOTE(k), { KEY_##k } \
    }
#define MEDIAKEY_TO_MAP(k)                               \
    {                                                    \
        QUOTE(k), { KEY_MEDIA_##k[0], KEY_MEDIA_##k[1] } \
    }
#define START_END_KEY \
    {                 \
        "", { 0 }     \
    }
    const KeyMap_t NeedReleaseKeys{
        {KEY_TO_MAP(LEFT_CTRL), KEY_TO_MAP(LEFT_SHIFT), KEY_TO_MAP(LEFT_ALT), KEY_TO_MAP(LEFT_GUI), KEY_TO_MAP(RIGHT_CTRL), KEY_TO_MAP(RIGHT_SHIFT), KEY_TO_MAP(RIGHT_ALT), KEY_TO_MAP(RIGHT_GUI)}};
    const KeyMap_t NeedDoubleBytes{
        {MEDIAKEY_TO_MAP(MUTE),
         MEDIAKEY_TO_MAP(VOLUME_DOWN),
         MEDIAKEY_TO_MAP(VOLUME_UP),
         MEDIAKEY_TO_MAP(PLAY_PAUSE),
         MEDIAKEY_TO_MAP(STOP),
         MEDIAKEY_TO_MAP(NEXT_TRACK),
         MEDIAKEY_TO_MAP(PREVIOUS_TRACK),
         MEDIAKEY_TO_MAP(WWW_HOME),
         MEDIAKEY_TO_MAP(LOCAL_MACHINE_BROWSER),
         MEDIAKEY_TO_MAP(CALCULATOR),
         MEDIAKEY_TO_MAP(WWW_BOOKMARKS),
         MEDIAKEY_TO_MAP(WWW_SEARCH),
         MEDIAKEY_TO_MAP(WWW_STOP),
         MEDIAKEY_TO_MAP(WWW_BACK),
         MEDIAKEY_TO_MAP(CONSUMER_CONTROL_CONFIGURATION),
         MEDIAKEY_TO_MAP(EMAIL_READER)}};
    const KeyMap_t KeyMap = {
        {MEDIAKEY_TO_MAP(MUTE), MEDIAKEY_TO_MAP(VOLUME_DOWN), MEDIAKEY_TO_MAP(VOLUME_UP), MEDIAKEY_TO_MAP(PLAY_PAUSE), MEDIAKEY_TO_MAP(STOP), MEDIAKEY_TO_MAP(NEXT_TRACK), MEDIAKEY_TO_MAP(PREVIOUS_TRACK), MEDIAKEY_TO_MAP(WWW_HOME), MEDIAKEY_TO_MAP(LOCAL_MACHINE_BROWSER), MEDIAKEY_TO_MAP(CALCULATOR), MEDIAKEY_TO_MAP(WWW_BOOKMARKS), MEDIAKEY_TO_MAP(WWW_SEARCH), MEDIAKEY_TO_MAP(WWW_STOP), MEDIAKEY_TO_MAP(WWW_BACK), MEDIAKEY_TO_MAP(CONSUMER_CONTROL_CONFIGURATION), MEDIAKEY_TO_MAP(EMAIL_READER),
         KEY_TO_MAP(F1), KEY_TO_MAP(F2), KEY_TO_MAP(F3), KEY_TO_MAP(F4), KEY_TO_MAP(F5), KEY_TO_MAP(F6), KEY_TO_MAP(F7), KEY_TO_MAP(F8), KEY_TO_MAP(F9), KEY_TO_MAP(F10), KEY_TO_MAP(F11), KEY_TO_MAP(F12), KEY_TO_MAP(F13), KEY_TO_MAP(F14), KEY_TO_MAP(F15), KEY_TO_MAP(F16), KEY_TO_MAP(F17), KEY_TO_MAP(F18), KEY_TO_MAP(F19), KEY_TO_MAP(F20), KEY_TO_MAP(F21), KEY_TO_MAP(F22), KEY_TO_MAP(F23), KEY_TO_MAP(F24),
         KEY_TO_MAP(UP_ARROW), KEY_TO_MAP(DOWN_ARROW), KEY_TO_MAP(LEFT_ARROW), KEY_TO_MAP(RIGHT_ARROW), KEY_TO_MAP(BACKSPACE), KEY_TO_MAP(TAB), KEY_TO_MAP(RETURN), KEY_TO_MAP(PAGE_UP), KEY_TO_MAP(PAGE_DOWN), KEY_TO_MAP(DELETE),
         KEY_TO_MAP(LEFT_CTRL), KEY_TO_MAP(LEFT_SHIFT), KEY_TO_MAP(LEFT_ALT), KEY_TO_MAP(LEFT_GUI), KEY_TO_MAP(RIGHT_CTRL), KEY_TO_MAP(RIGHT_SHIFT), KEY_TO_MAP(RIGHT_ALT), KEY_TO_MAP(RIGHT_GUI)}};

    FTAction::~FTAction()
    {

        Values.clear();
        Parameters.clear();
    }
    void FTAction::InitConstants()
    {
        LOC_LOGD(module, "Initializing actions constants");
        rebootSystem = FTAction(ParametersList_t({"REBOOT"}));
    }
    cJSON *KeyNamesJson()
    {
        cJSON *doc = cJSON_CreateArray();
        for (auto k : KeyMap)
        {
            cJSON_AddItemToArray(doc, cJSON_CreateString(k.first));
        }
        return doc;
    }

    bool FTAction::IsValidKey(const char *name, char **foundValue)
    {
        bool found = false;
        FREE_AND_ASSIGNED_IF_PASSED(foundValue, NULL);
        if (ISNULLSTRING(name))
        {
            LOC_LOGD(module, "Empty key passed");
        }
        else
        {
            char *foundKey = NULL;
            for (auto k : KeyMap)
            {
                if (strcmp(k.first, name) == 0)
                {
                    found = true;
                    break;
                }
            }
            if (found)
            {
                LOC_LOGD(module, "Found Key %s", foundKey);
                if (foundValue)
                {
                    *foundValue = ps_strdup(name);
                }
                else
                {
                    // Caller did not request the resulting key
                    // so free up memory
                    LOC_LOGD(module, "Result key was not requested. Freeing up memory");
                    FREE_AND_NULL(foundKey);
                }
            }
        }
        LOC_LOGD(module, "Key %s was %s parsed.", STRING_OR_DEFAULT(name, ""), found ? "successfully" : "not");
        return found;
    }

    bool FTAction::ParseToken(const char *token, std::vector<FTAction *> &actions)
    {
        PrintMemInfo(__FUNCTION__, __LINE__);
        bool success = false;
        ParametersList_t parameters;
        uint16_t delay = 0;
        SplitParameters(token, parameters);
        const char *tokenName = GetParameter(0, parameters).c_str();

        if (parameters.size() > 1 && atol(GetParameter(1, parameters).c_str()) > 0)
        {
            delay = atol(GetParameter(1, parameters).c_str());
        }

        for (auto k : KeyMap)
        {
            if (strcmp(k.first, tokenName) == 0)
            {
                PrintMemInfo(__FUNCTION__, __LINE__);
                LOC_LOGD(module, "Found Keyboard symbol %s, press delay %d ", tokenName, delay);
                actions.push_back(new FTAction(k.first, k.second));
                actions.back()->HoldTime = delay;
                PrintMemInfo(__FUNCTION__, __LINE__);
                success = true;
                break;
            }
        }
        if (!success && delay > 0)
        {
            if (strlen(tokenName) == 1)
            {
                PrintMemInfo(__FUNCTION__, __LINE__);
                LOC_LOGD(module, "Found Keyboard symbol %s with delay %d", tokenName, delay);
                actions.push_back(new FTAction((tokenName), KeyValue_t({(unsigned char)tokenName[0]})));
                actions.back()->HoldTime = delay;
                PrintMemInfo(__FUNCTION__, __LINE__);
                success = true;
            }
        }
        if (!success)
        {
            if (parameters.size() == 0)
            {
                LOC_LOGE(module, "No parameters or action name found. ");
            }
            else if (IsActionCallback(parameters))
            {
                LOC_LOGD(module, "Found user Action %s with %d parameter(s)", GetParameter(0, parameters).c_str(), parameters.size() - 1);
                actions.push_back(new FTAction(parameters));
                success = true;
            }
        }
        if (!success)
        {
            LOC_LOGE(module, "Invalid local action type %s with %d parameter(s)", token, parameters.size());
        }
        PrintMemInfo(__FUNCTION__, __LINE__);
        return success;
    }
    const char *FTAction::ActionName()
    {
        LOC_LOGV(module, "Getting Action name from %s", toString());
        return GetParameter(0).c_str();
    }
    const char *FTAction::FirstParameter()
    {
        LOC_LOGV(module, "Getting first parameter from %s", toString());
        return GetParameter(1).c_str();
    }
    const char *FTAction::SecondParameter()
    {
        LOC_LOGV(module, "Getting second parameter from %s", toString());
        return GetParameter(2).c_str();
    }
    const char *FTAction::ThirdParameter()
    {
        LOC_LOGV(module, "Getting third parameter from %s", toString());
        return GetParameter(3).c_str();
    }
    std::string &FTAction::ActionNameStr()
    {
        LOC_LOGV(module, "Getting Action name from %s", toString());
        return GetParameter(0);
    }
    std::string &FTAction::FirstParameterStr()
    {
        return GetParameter(1);
    }
    std::string &FTAction::SecondParameterStr()
    {
        return GetParameter(2);
    }
    std::string &FTAction::ThirdParameterStr()
    {
        return GetParameter(3);
    }
    std::string &FTAction::GetParameter(int index, ParametersList_t &parameters)
    {
        LOC_LOGV(module, "Getting parameter %d a total of %d entries", index, parameters.size());
        ParametersList_t::iterator it;
        if (parameters.size() > index)
        {
            it = parameters.begin();
            std::advance(it, index);
            if (it != parameters.end())
            {
                LOC_LOGV(module, "Found entry %d : %s", index, (*it).c_str());
                return *it;
            }
        }
        LOC_LOGE(module, "No parameter #%d was found. Size is %d", index, parameters.size());
        return emptyString;
    }
    std::string &FTAction::GetParameter(int index)
    {
        return GetParameter(index, Parameters);
    }

    bool FTAction::SplitParameters(const char *parmString, ParametersList_t &parameters)
    {
        PrintMemInfo(__FUNCTION__, __LINE__);
        char *token = ps_strdup(parmString);
        char *separators = ps_strdup(parmString);
        const char *p = parmString;
        const char *s = parmString;
        int res = 0;
        int resSeparators = 0;
        do
        {
            res = sscanf(p, splitterFormat, token);
            if (res > 0)
            {
                p += strlen(token);
                resSeparators = sscanf(p, separatorFormat, separators);
                if (resSeparators > 0)
                {
                    p += strlen(separators);
                }
                else
                {
                    strcpy(separators, "");
                }
                LOC_LOGD(module, "Found parameter : %s", STRING_OR_DEFAULT(token, ""));
                parameters.push_back(token);
            }
            else
            {
                break;
            }
        } while (p && *p);

        FREE_AND_NULL(token);
        FREE_AND_NULL(separators);
        PrintMemInfo(__FUNCTION__, __LINE__);
        LOC_LOGV(module, "End of parameters list");
        return true;
    }

    bool FTAction::KeyNeedsRelease(const char *keyName)
    {
        for (auto k : NeedReleaseKeys)
        {
            if (strcmp(k.first, keyName) == 0)
            {
                return true;
            }
        }
        return false;
    }
    bool FTAction::KeyIsDoubleBytes(const char *keyName)
    {
        for (auto k : NeedDoubleBytes)
        {
            if (strcmp(k.first, keyName) == 0)
            {
                return true;
            }
        }
        return false;
    }
    bool checkForStop()
    {
        if (isTouched())
        {
            EmptyQueue();
            return true;
        }
        return false;
    }
    FTAction::FTAction(const char *keyName, const KeyValue_t &values)
    {
        PrintMemInfo(__FUNCTION__, __LINE__);
        Type = ActionTypes::KEYBOARD;
        if (ISNULLSTRING(keyName))
        {
            LOC_LOGE(module, "Empty key name received");
        }
        else
        {
            LOC_LOGD(module, "New action with key names %s, length of %d", keyName, Values.size());
            Parameters.push_back(keyName);
            PrintMemInfo(__FUNCTION__, __LINE__);
            NeedsRelease = KeyNeedsRelease(keyName);
            NeedsDoubleBytes = KeyIsDoubleBytes(keyName);
        }
        PrintMemInfo(__FUNCTION__, __LINE__);
        Values = values;
    }
    FTAction::FTAction(const KeyValue_t &values)
    {
        Type = ActionTypes::KEYBOARD;
        Values = values;
    }
    FTAction::FTAction(const ParametersList_t &parameters)
    {
        Type = ActionTypes::LOCAL;
        Parameters = parameters;
    }
    void FTAction::Execute()
    {
        bool wasStopped = false;
        MediaKeyReport MediaKey;
        LOC_LOGI(module, "Executing Action %s", toString());
        if (checkForStop())
        {
            return;
        }
        switch (Type)
        {
        case ActionTypes::NONE:
            break;
        case ActionTypes::KEYBOARD:
            if (!bleKeyboard.isConnected())
            {
                LOC_LOGW(module, "Skipping action %s. Bluetooth Keyboard not connected", toString());
                return;
            }
            if (NeedsDoubleBytes)
            {
                MediaKey[0] = Values[0];
                MediaKey[1] = Values[1];
                if (NeedsRelease)
                {
                    bleKeyboard.press(MediaKey);
                }
                else
                {
                    bleKeyboard.write(MediaKey);
                }
                delay(generalconfig.keyDelay);
            }
            else
            {
                if (NeedsRelease)
                {
                    for (auto ks : Values)
                    {
                        // Use keyboard press for each character
                        // as this method does not release each key
                        // individually
                        bleKeyboard.press(ks);
                        delay(generalconfig.keyDelay);
                        wasStopped=checkForStop();
                        if(wasStopped) break;
                        
                    }
                }
                else
                {
                    for (auto ks : Values)
                    {
                        if (HoldTime > 0)
                        {
                            LOC_LOGI(module, "Pressing key with hold of %d ms", HoldTime);
                        }
                        bleKeyboard.press(ks);
                        delay(HoldTime);
                        bleKeyboard.release(ks);
                        delay(generalconfig.keyDelay);
                        wasStopped=checkForStop();
                        if(wasStopped) break;
                    }
                }
            }
            break;
        case ActionTypes::LOCAL:
            CallActionCallback(false);
        default:
            break;
        }
        if (wasStopped && NeedsRelease)
        {
            LOC_LOGI(module,"Releasing all keys");
            bleKeyboard.releaseAll();
        }
    }

    const char *FTAction::toString()
    {
        switch (Type)
        {
        case ActionTypes::NONE:
            break;
        case ActionTypes::LOCAL:
        case ActionTypes::KEYBOARD:
            snprintf(printBuffer, sizeof(printBuffer), "%s:%s:",IsScreen()?"SCREEN":"LOCAL", enum_to_string(Type));
            for (auto p : Parameters)
            {
                snprintf(printBuffer, sizeof(printBuffer), "%s %s", printBuffer, p.c_str());
            }
            snprintf(printBuffer, sizeof(printBuffer), "%s, Values count: %d, %s", printBuffer, Values.size(), NeedsRelease?"NEEDS RELEASE":"");
            break;
        default:
            break;
        }
        return printBuffer;
    }

    FTAction *PopScreenQueue()
    {
        FTAction *Action = NULL;
        if (QueueLock(portMAX_DELAY / portTICK_PERIOD_MS))
        {
            if (!ScreenQueue.empty())
            {
                LOC_LOGV(module, "Screen Action Queue Length : %d", ScreenQueue.size());
                Action = ScreenQueue.front();
                ScreenQueue.pop();
                LOC_LOGV(module, "Screen Action Queue Length : %d", ScreenQueue.size());
            }
            QueueUnlock();
        }
        else
        {
            LOC_LOGE(module, "Unable to screen lock Action queue");
        }
        return Action;
    }
    void EmptyQueue()
    {
        if (QueueLock(portMAX_DELAY / portTICK_PERIOD_MS))
        {
            while (!Queue.empty())
            {
                Queue.pop();
            }
            QueueUnlock();
        }
    }
    size_t QueueSize()
    {
        return Queue.size() + ScreenQueue.size();
    }
    FTAction *PopQueue()
    {
        FTAction *Action = NULL;

        if (QueueLock(portMAX_DELAY / portTICK_PERIOD_MS))
        {
            if (!Queue.empty())
            {
                LOC_LOGV(module, "Action Queue Length : %d", Queue.size());
                Action = Queue.front();
                Queue.pop();
                LOC_LOGV(module, "Action Queue Length : %d", Queue.size());
            }
            QueueUnlock();
        }
        else
        {
            LOC_LOGE(module, "Unable to lock Action queue");
        }
        return Action;
    }
    bool QueueLock(TickType_t xTicksToWait)
    {
        LOC_LOGV(module, "Locking Action Queue object");
        if (xSemaphoreTake(xQueueSemaphore, xTicksToWait) == pdTRUE)
        {
            LOC_LOGV(module, "Action Queue object  locked!");
            return true;
        }
        else
        {
            LOC_LOGE(module, "Unable to lock the Action queue object");
            return false;
        }
    }

    void QueueUnlock()
    {
        LOC_LOGV(module, "Unlocking the Action queue object");
        xSemaphoreGive(xQueueSemaphore);
    }
    bool QueueAction(FTAction *action)
    {
        if (!QueueLock(100 / portTICK_PERIOD_MS))
        {
            LOC_LOGE(module, "Unable to queue new action ");
            return false;
        }
        if (action->IsScreen())
        {
            LOC_LOGD(module, "Pushing action %s to screen queue", action->toString());
            ScreenQueue.push(action);
        }
        else
        {
            LOC_LOGD(module, "Pushing action %s to keyboard queue", action->toString());
            Queue.push(action);
        }

        QueueUnlock();
        return true;
    }

    bool FTAction::CallActionCallback(ParametersList_t &parameters, FTAction *action, bool checkOnly)
    {
        ActionCallbackFn_t callbackFn = NULL;
        PrintMemInfo(__FUNCTION__, __LINE__);
        std::string name = GetParameter(0, parameters);
        auto callback = FreeTouchDeck::UserActions.find(name.c_str());
        if (callback == FreeTouchDeck::UserActions.end())
        {
            LOC_LOGW(module, "Find failed. Trying to iterate entries");
            for (auto c : FreeTouchDeck::UserActions)
            {
                if (c.first == name)
                {
                    callbackFn = c.second;
                    LOC_LOGD(module, "Found the callback with a loop in the map values");
                    break;
                }
            }
        }
        else
        {
            LOC_LOGD(module, "Found the callback in the map");
            callbackFn = callback->second;
        }
        if (callbackFn)
        {
            if (checkOnly)
            {
                LOC_LOGD(module, "Check only, returning success");
                return true;
            }
            else if (!action)
            {
                LOC_LOGE(module, "Action pointer is null. Unable to invoke callback!");
            }
            else
            {
                LOC_LOGD(module, "Calling function %s", name.c_str());
                bool res = callbackFn(action);
                PrintMemInfo(__FUNCTION__, __LINE__);
                return res;
            }
        }
        else
        {
            LOC_LOGE(module, "Invalid callback name %s. Valid callbacks are: ", name.c_str());
            for (auto c : FreeTouchDeck::UserActions)
            {
                LOC_LOGE(module, "    %s", c.first.c_str());
            }
        }
        return false;
    }
    bool FTAction::IsActionCallback(ParametersList_t &parameters)
    {
        return CallActionCallback(parameters, NULL, true);
    }
    bool FTAction::CallActionCallback(bool checkOnly)
    {
        return CallActionCallback(Parameters, this, checkOnly);
    }
    cJSON *UserActionsJson()
    {
        cJSON *doc = cJSON_CreateArray();
        for (auto a : UserActions)
        {
            cJSON_AddItemToArray(doc, cJSON_CreateString(a.first.c_str()));
        }
        return doc;
    }
}