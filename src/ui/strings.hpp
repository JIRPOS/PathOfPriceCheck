#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

/// The text *this application* writes, as against the text the game does.
///
/// Two different problems wear the word "localisation" here and they share nothing. Reading a
/// translated client is `data::Lexicon`: game text, authored by GGG, shipped in the data
/// bundle, and load-bearing — get a word wrong and an item fails to parse. This is the other
/// half: our own labels and help lines, authored by us, compiled in, and cosmetic — get one
/// wrong and a button reads oddly. Neither buys anything toward the other, which is why they
/// are separate settings (`Config::client_language`, `Config::ui_language`): a player running
/// a Russian client may want an English panel, or the reverse.
///
/// Every table is compiled in. They are a few kilobytes each against an executable that
/// already embeds four typefaces, and a per-language build would contradict the rule that a
/// new league needs a data build rather than a release.
namespace ppc::ui {

/// One piece of our own text. Values with a `%` in their English form are format strings and
/// are handed to `ImGui::Text`, so a translation must keep the same conversions in the same
/// order — a table with a mismatched one is a crash, not a typo.
enum class Msg : uint16_t {
    SettingsTitle,
    Close,

    TabGeneral,
    TabPriceCheck,
    TabQuickPaste,
    TabMapCheck,
    TabApplication,

    SectionAccount,
    SectionLanguage,
    SectionAppearance,
    SectionTradeSearch,
    SectionFilterRanges,
    SectionHotkeys,
    SectionPastes,
    SectionPricePanel,
    SectionGameData,
    SectionUpdates,
    SectionDiagnostics,

    League,
    Refresh,
    JustRefreshed,      ///< "%d" — seconds left on the cooldown
    FetchingLeagues,
    LeagueCount,        ///< "%zu"
    LeagueError,        ///< "%s" — what curl said
    OfflineList,
    Account,
    AccountHint,
    AccountExpected,

    ClientLanguage,
    ClientLanguageHelp,
    InterfaceLanguage,
    FollowClient,
    LanguageNeedsRestart,
    NoLexicon,

    Listings,
    FetchTop,
    TopN,               ///< "%d"
    RequestCost,        ///< "%d", "%s" (plural), "%d"
    AutoSearch,
    AutoSearchOn,
    AutoSearchOff,

    FilterRangesHelp,
    Minimum,
    Maximum,
    BoundHelp,

    HotkeyPriceCheck,
    HotkeySettings,
    HotkeyQuickPaste,
    HotkeyMapCheck,
    PressKeys,

    ReduceTransparency,
    ReduceTransparencyHelp,
    StatusMarker,
    StatusMarkerHelp,

    PanelHelp,
    PanelWidth,
    StashEdge,
    InventoryEdge,

    PasteListHelp,
    PasteNone,            ///< Settings, with nothing in the list yet
    PasteSlotsLeft,       ///< "%zu", "%zu" — active pastes and the ceiling
    PasteSlotsFull,       ///< "%zu" — the ceiling
    PasteUntitled,
    PasteEmptyBody,
    PasteNew,
    PasteEdit,
    PasteDelete,
    PasteReorder,
    PasteHeading,
    PasteHeadingHint,
    PasteBody,
    PasteBodyHint,
    PasteDone,
    PasteCancel,
    PasteTooLong,         ///< "%zu" — the byte ceiling
    QuickPasteNone,       ///< the popup, with nothing enabled to offer
    QuickPasteAdd,

    SectionMapProfiles,
    SectionMapModifiers,
    MapProfile,
    MapProfileNone,
    MapProfileNew,
    MapProfileName,
    MapProfileNameHint,
    MapProfileCopyFrom,
    MapProfileEmpty,
    MapProfileCreate,
    MapProfileCancel,
    MapProfileDelete,
    MapProfileDeleteAsk,     ///< "%s" — the profile's name
    MapProfileDeleteWarn,    ///< "%zu" — how many ratings go with it
    MapFilterHint,
    MapFilterHelp,
    MapSearchSyntax,         ///< the `?` beside the search box: the game's own search rules
    MapFilterSetAside,       ///< "%s" — the terms that ask about the item, not a modifier
    MapVerdictInherited,     ///< a row lit by a rating made on a shorter affix inside it
    MapPropose,
    MapProposeTip,
    MapProposeNothing,
    MapProposeCounts,        ///< "%d", "%d" — deadly and safe
    MapProposeApply,
    MapPoolCount,            ///< "%zu", "%zu" — shown and held
    MapPoolNoData,
    MapPoolEmpty,
    MapNoProfile,
    MapNoProfileHelp,
    MapRatedCount,           ///< "%zu"
    MapCheckTitle,
    MapRateHint,
    MapUnresolved,
    MapOutlookNoMods,
    MapOutlookUnrated,
    MapOutlookSafe,
    MapOutlookSafeUnrated,
    MapOutlookLikely,
    MapOutlookCareful,
    MapOutlookFatal,

    Bundle,
    Downloading,        ///< "%.1f", "%.1f" — megabytes done and total
    DownloadingPlain,
    CheckingUpdates,
    Installing,
    NoDataInstalled,
    NotDownloadedYet,
    CheckNow,
    ParsingWorksWithout,
    StatWordings,       ///< "%zu"
    UniqueModsCredit,   ///< "%s" — the credit the bundle states

    Application,
    UpToDate,
    UpdateChecking,
    UpdateDownloading,  ///< "%.1f", "%.1f" — megabytes done and total
    UpdateReady,        ///< "%s" — the new version
    UpdateAvailable,    ///< "%s" — the new version
    UpdateOfferHelp,      ///< the offer's reason, one per `update::Offered`
    UpdateOfferUnmanaged,
    UpdateOfferNoAsset,
    RestartNow,
    OpenReleasePage,
    AutoUpdate,
    AutoUpdateHelp,

    DebugLogging,
    LogOpenFailed,
    DebugLogHelp,

    Save,
    OpenTheFolder,
    Count
};

/// The text for `m`, in the selected language and in English wherever that language has
/// nothing to say. Never null and never empty — an untranslated entry falls back rather than
/// leaving a blank control.
const char* text(Msg m);

/// Draw our own text in `lang`, or follow the client when it is "auto" or a language nothing
/// is compiled in for. `client` is `Config::client_language`, which is what "auto" follows.
void set_language(std::string_view lang, std::string_view client);

/// Which language is actually being drawn, after "auto" and the fallback are resolved.
std::string_view language();

/// Every language a table is compiled in for, English first. This is the *interface* list and
/// is deliberately not the client list: the two are filled in one at a time and by different
/// work, so a language can be readable long before it is speakable, or the other way round.
std::span<const std::string_view> languages();

} // namespace ppc::ui
