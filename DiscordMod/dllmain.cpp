#include <format>

#include <UnrealDef.hpp>
#include <Mod/CppUserModBase.hpp>
#include <DynamicOutput/DynamicOutput.hpp>

#include "discord_rpc.h"

#define DISCORD_APPLICATION_ID "1158533300550369470"
#define PAYDAY_3_STEAM_APPID "1272080"

using namespace RC;
using namespace RC::Unreal;

struct FSBZPartyMember
{
    /* ... */
};

enum Status : uint8_t
{
    GameStarting,
    InMenu,
    // InMatchMaking,
    InPrePlanning,
    InHeist
};

std::wstring CharToWString(const char *src)
{
    return std::wstring(src, src + strlen(src));
}

const char *WCharToChar(const wchar_t *src)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter; // that shit is deprecated but who care (not me)
    std::string narrowStr = converter.to_bytes(src);
    return narrowStr.c_str();
}

class DiscordMod : public RC::CppUserModBase
{
public:
    Status status = Status::GameStarting;
    std::optional<StringType> heist_name = std::nullopt;
    std::optional<StringType> difficulty_name = std::nullopt;
    std::optional<int> party_size = 1;
    std::optional<const wchar_t *> partyId = std::nullopt;
    std::optional<const wchar_t *> joinSecret = std::nullopt;
    time_t startTimestamp = std::time(0);

    DiscordMod() : CppUserModBase()
    {
        ModName = STR("Discord");
        ModVersion = STR("1.0");
        ModDescription = STR("Discord Rich Presence Mod.");
        ModAuthors = STR("Mathi");

        DiscordEventHandlers handlers;
        memset(&handlers, 0, sizeof(handlers));
        handlers.ready = [](const DiscordUser *request)
        { Output::send<LogLevel::Verbose>(STR("[Discord:Event] Ready ({}#{} - {})\n"), CharToWString(request->username), CharToWString(request->discriminator), CharToWString(request->userId)); };
        handlers.disconnected = [](int errorCode, const char *message)
        { Output::send<LogLevel::Verbose>(STR("[Discord:Event] Disconnected (code={}, message={})\n"), errorCode, CharToWString(message)); };
        handlers.errored = [](int errorCode, const char *message)
        { Output::send<LogLevel::Verbose>(STR("[Discord:Event] Error (code={}, message={})\n"), errorCode, CharToWString(message)); };
        handlers.joinGame = [](const char *joinSecret)
        {
            Output::send<LogLevel::Verbose>(STR("[Discord:Event] Join game (secret={})\n"), CharToWString(joinSecret));
            // ASBZBeaconActionPhaseClient->ClientJoinPartyByCode(PartyCode) ?
        };
        handlers.joinRequest = [](const DiscordUser *request)
        {
            Output::send<LogLevel::Verbose>(STR("[Discord:Event] Join request ({}#{} - {})\n"), CharToWString(request->username), CharToWString(request->discriminator), CharToWString(request->userId));
            Discord_Respond(request->userId, DISCORD_REPLY_IGNORE);
        };
        Discord_Initialize(DISCORD_APPLICATION_ID, &handlers, 1, PAYDAY_3_STEAM_APPID);

        Output::send<LogLevel::Verbose>(STR("[Discord] Init.\n"));
    }

    ~DiscordMod() override
    {
        Discord_Shutdown();
    }

    auto update_presence() -> void
    {
        DiscordRichPresence discordPresence;
        memset(&discordPresence, 0, sizeof(discordPresence));
        switch (status)
        {
        case Status::GameStarting:
            discordPresence.state = "Game Starting";
            break;
        case Status::InMenu:
            discordPresence.state = "In Menu";
            break;
        case Status::InPrePlanning:
            discordPresence.state = "In Pre-Planning";
            break;
        case Status::InHeist:
            discordPresence.state = "In Heist";
            if (heist_name.has_value() && difficulty_name.has_value())
            {
                auto formated = std::format(STR("{} | {}"), heist_name.value(), difficulty_name.value());
                discordPresence.details = WCharToChar(formated.c_str());
            }
            break;
        }
        discordPresence.startTimestamp = startTimestamp;
        discordPresence.largeImageKey = "pd3"; // TODO: add heist images
        // discordPresence.smallImageKey = "pd3"; // TODO: character mask images

        if (party_size.has_value())
            discordPresence.partySize = party_size.value();
        else
            discordPresence.partySize = 1;
        discordPresence.partyMax = 4; // TODO: non hard coded party max

        discordPresence.partyPrivacy = DISCORD_PARTY_PUBLIC; // TODO: private if offline or (ESBZOnlineJoinType) Private FriendsOnly InviteOnly
        if (partyId.has_value())
            discordPresence.partyId = WCharToChar(partyId.value());
        if (joinSecret.has_value())
            discordPresence.joinSecret = WCharToChar(joinSecret.value());

        Discord_UpdatePresence(&discordPresence);

#ifdef DISCORD_DISABLE_IO_THREAD
        Discord_UpdateConnection();
#endif
        Discord_RunCallbacks();
    }

    auto on_unreal_init() -> void override
    {
        Hook::RegisterBeginPlayPostCallback(
            [=](AActor *Context)
            {
                auto classPrivate = Context->GetClassPrivate();
                if (classPrivate == nullptr)
                    return;
                auto name = classPrivate->GetName();
                // Output::send<LogLevel::Verbose>(STR("[Discord] BeginPlayPostCallback: {}\n"), name);
                // TODO: mode precise timers
                if (name == STR("BP_GameModeMainMenu_C"))
                {
                    status = Status::InMenu;
                    startTimestamp = std::time(0);
                }
                else if (name == STR("SBZBeaconActionPhaseClient"))
                {
                    status = Status::InPrePlanning;
                    startTimestamp = std::time(0);
                }
                else if (name == STR("BP_PlayerController_C"))
                {
                    status = Status::InHeist;
                    startTimestamp = std::time(0);
                }
            });
    }

    // lazy delay for on_update
    uint8_t on_update_id = 0;
    auto on_update() -> void override
    {
        if (on_update_id != 255)
        {
            on_update_id++;
            return;
        }
        else
            on_update_id = 0;

        auto SBZPartyManager = UObjectGlobals::FindFirstOf(STR("SBZPartyManager"));
        if (SBZPartyManager != nullptr)
        {
            auto PartyId = SBZPartyManager->GetValuePtrByPropertyNameInChain<FString>(STR("PartyId"));
            if (PartyId != nullptr)
                partyId = PartyId->GetCharArray();
            else
                partyId = std::nullopt;

            auto PartyCode = SBZPartyManager->GetValuePtrByPropertyNameInChain<FString>(STR("PartyCode"));
            if (PartyCode != nullptr)
                joinSecret = PartyCode->GetCharArray();
            else
                joinSecret = std::nullopt;
        }

        if (status == Status::InMenu)
        {
            if (SBZPartyManager != nullptr)
            {
                auto PartyMembers = SBZPartyManager->GetValuePtrByPropertyNameInChain<Unreal::TArray<FSBZPartyMember>>(STR("PartyMembers"));
                if (PartyMembers != nullptr)
                    party_size = PartyMembers->Num();
                else
                    party_size = 1;
            }
        }
        else if (status == Status::InPrePlanning)
        {
            // TODO: get heist, difficulty and players in pre-planning screen
        }
        else if (status == Status::InHeist)
        {
            // TODO: move this somewhere else? maybe hook WBP_UI_HUD_Overlay_Right_C:OnInitialized?
            auto WBP_UI_HUD_Overlay_Right_C = UObjectGlobals::FindObject(
                STR("WBP_UI_HUD_Overlay_Right_C"),
                nullptr,
                EObjectFlags::RF_NoFlags,
                EObjectFlags::RF_ClassDefaultObject | EObjectFlags::RF_WasLoaded | EObjectFlags::RF_LoadCompleted);

            if (WBP_UI_HUD_Overlay_Right_C != nullptr)
            {
                auto Text_Difficulty = *WBP_UI_HUD_Overlay_Right_C->GetValuePtrByPropertyNameInChain<UObject *>(STR("Text_Difficulty"));
                if (Text_Difficulty != nullptr)
                {
                    auto Text = Text_Difficulty->GetValuePtrByPropertyName<FText>(STR("Text"));
                    if (Text != nullptr)
                        difficulty_name = Text->ToString();
                    else
                        difficulty_name = std::nullopt;
                }
                else
                    difficulty_name = std::nullopt;

                auto Text_LevelName = *WBP_UI_HUD_Overlay_Right_C->GetValuePtrByPropertyNameInChain<UObject *>(STR("Text_LevelName"));
                if (Text_LevelName != nullptr)
                {
                    auto Text = Text_LevelName->GetValuePtrByPropertyName<FText>(STR("Text"));
                    if (Text != nullptr)
                        heist_name = Text->ToString();
                    else
                        heist_name = std::nullopt;
                }
                else
                    heist_name = std::nullopt;
            }
            else
                difficulty_name = heist_name = std::nullopt;

            auto PD3HUDPartyContainerWidget = UObjectGlobals::FindFirstOf(STR("PD3HUDPartyContainerWidget"));

            if (PD3HUDPartyContainerWidget != nullptr)
            {
                auto Panel_PartyPlayerWidgetContainer = *PD3HUDPartyContainerWidget->GetValuePtrByPropertyNameInChain<UObject *>(STR("Panel_PartyPlayerWidgetContainer"));
                if (Panel_PartyPlayerWidgetContainer != nullptr)
                {
                    auto Slots = Panel_PartyPlayerWidgetContainer->GetValuePtrByPropertyNameInChain<Unreal::TArray<UObject *>>(STR("Slots"));
                    if (Slots != nullptr)
                        party_size = Slots->Num() + 1;
                    else
                        party_size = 1;
                }
            }
        }

        update_presence();
    }
};

#define MOD_EXPORT __declspec(dllexport)
extern "C"
{
    MOD_EXPORT RC::CppUserModBase *start_mod() { return new DiscordMod(); }
    MOD_EXPORT void uninstall_mod(RC::CppUserModBase *mod) { delete mod; }
}
