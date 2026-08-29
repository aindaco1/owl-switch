#include "modules/karaoke/KaraokeBackend.h"
#include "tools/HelperResolver.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QtTest>

#include <algorithm>

class KaraokeBackendTest : public QObject {
    Q_OBJECT

private slots:
    void cleanedTitle_data();
    void cleanedTitle();
    void cleansProviderTitles();
    void validatesCatalogSongs();
    void deduplicatesCatalogBySourcePriority();
    void validatesCatalogSizes();
    void resolvesPinnedYoutubePlaybackArguments();
    void loadsFreshCatalogCache();
    void loadsLegacyMultiSourceCatalogCache();
    void loadsLegacyFourSourceCatalogCache();
    void loadsLegacyFiveSourceCatalogCache();
    void loadsLegacySixSourceCatalogCache();
    void loadsLegacyNineSourceCatalogCache();
    void loadsLegacyElevenSourceCatalogCache();
    void loadsLegacyThirteenSourceCatalogCache();
    void loadsLegacyFifteenSourceCatalogCache();
    void loadsLegacySixteenSourceCatalogCache();
    void loadsLegacySeventeenSourceCatalogCache();
    void loadsCurrentCatalogCache();
    void persistsAndMutatesQueue();
    void usesCachedPlaybackMedia();
    void rejectsInvalidQueueEntries();
    void rollsBackQueueOnPersistenceFailure();
    void refreshesLiveCatalog();
    void prefetchesLivePlaybackMedia();
};

void KaraokeBackendTest::cleanedTitle_data()
{
    QTest::addColumn<QString>("raw");
    QTest::addColumn<QString>("expected");

    QTest::newRow("standard")
        << QStringLiteral("Death From Above 1979 - Romantic Rights (Funbox Karaoke, 2004)")
        << QStringLiteral("Death From Above 1979 - Romantic Rights (2004)");
    QTest::newRow("multiple years")
        << QStringLiteral("Artist - Song (Funbox Karaoke, 1999/2001)")
        << QStringLiteral("Artist - Song (1999/2001)");
    QTest::newRow("case insensitive")
        << QStringLiteral("Artist - Song (funbox karaoke, 2024)")
        << QStringLiteral("Artist - Song (2024)");
    QTest::newRow("obskure standard")
        << QStringLiteral("The Mars Volta - Plant a Nail in the Navel Stream - Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("The Mars Volta - Plant a Nail in the Navel Stream");
    QTest::newRow("obskure older unbranded")
        << QStringLiteral("Artist - Song - Karaoke Instrumental Lyrics")
        << QStringLiteral("Artist - Song");
    QTest::newRow("obskure version variant")
        << QStringLiteral("Artist - Song - Karaoke Version - Instrumental Lyrics - ObsKure")
        << QStringLiteral("Artist - Song");
    QTest::newRow("obskure portuguese variant")
        << QStringLiteral("Artist - Song - Karaoke Instrumental Letra - Melhor Versão - ObsKure")
        << QStringLiteral("Artist - Song");
    QTest::newRow("obskure best version")
        << QStringLiteral(
               "Queen - Bohemian Rhapsody - Best Version - Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("Queen - Bohemian Rhapsody");
    QTest::newRow("obskure parenthesized best version")
        << QStringLiteral(
               "The Weeknd - Blinding Lights (Best Version) - Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("The Weeknd - Blinding Lights");
    QTest::newRow("obskure duet branding")
        << QStringLiteral(
               "Maluma ft. Ty Dolla $ign - Tu Vecina - Duet Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("Maluma Ft. Ty Dolla $ign - Tu Vecina");
    QTest::newRow("obskure best duet branding")
        << QStringLiteral(
               "TV On The Radio - Wolf Like Me - Best Version - Duet Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("TV On The Radio - Wolf Like Me");
    QTest::newRow("obskure original sound")
        << QStringLiteral(
               "INXS - Never Tear Us Apart - Original Sound - Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("INXS - Never Tear Us Apart");
    QTest::newRow("obskure portuguese descriptor chain")
        << QStringLiteral(
               "O Rappa - O Que Sobrou Do Céu - Dueto - Som Original - Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("O Rappa - O Que Sobrou Do Céu");
    QTest::newRow("lemmy caution")
        << QStringLiteral("Carly Simon – You Belong to Me (karaoke)")
        << QStringLiteral("Carly Simon – You Belong to Me");
    QTest::newRow("lemmy caution inline marker")
        << QStringLiteral("New Order -  Fine Time (karaoke)  (7 inch version)")
        << QStringLiteral("New Order - Fine Time (7\")");
    QTest::newRow("seven inch version")
        << QStringLiteral("Fine Time (7 Inch Version)")
        << QStringLiteral("Fine Time (7\")");
    QTest::newRow("lemmy caution video version")
        << QStringLiteral(
               "New Order - Bizarre Love Triangle (karaoke) video version")
        << QStringLiteral("New Order - Bizarre Love Triangle (Video)");
    QTest::newRow("video version")
        << QStringLiteral("Bizarre Love Triangle Video Version")
        << QStringLiteral("Bizarre Love Triangle (Video)");
    QTest::newRow("funbox album version")
        << QStringLiteral(
               "Afroman - Crazy Rap / Colt 45 [Album Version] (Funbox Karaoke, 2001)")
        << QStringLiteral("Afroman - Crazy Rap / Colt 45 (Album) (2001)");
    QTest::newRow("funbox live version")
        << QStringLiteral(
               "AURORA - Teardrop [Live Version] (Funbox Karaoke, 2017)")
        << QStringLiteral("AURORA - Teardrop (Live) (2017)");
    QTest::newRow("funbox original version")
        << QStringLiteral(
               "Beastie Boys - Body Movin' [Original Version] (Funbox Karaoke, 1998)")
        << QStringLiteral("Beastie Boys - Body Movin' (Original) (1998)");
    QTest::newRow("single version")
        << QStringLiteral(
               "Beach House - Used To Be (Single Version) - Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("Beach House - Used To Be (Single)");
    QTest::newRow("official version and known possessive")
        << QStringLiteral("Bauhaus - Bela Lugosis Dead (Official Version)")
        << QStringLiteral("Bauhaus - Bela Lugosi's Dead (Official)");
    QTest::newRow("piano version")
        << QStringLiteral(
               "Black Label Society - The Last Goodbye (Piano Version) - Karaoke Instrumental Lyrics - ObsKure")
        << QStringLiteral("Black Label Society - The Last Goodbye (Piano)");
    QTest::newRow("year version")
        << QStringLiteral(
               "Bob Dylan - Every Grain of Sand (1980 Version) (Karaoke)")
        << QStringLiteral("Bob Dylan - Every Grain of Sand (1980)");
    QTest::newRow("companion version")
        << QStringLiteral(
               "Bright Eyes - Falling Out Of Love At This Volume (Companion Version) (Karaoke)")
        << QStringLiteral(
               "Bright Eyes - Falling Out Of Love At This Volume (Companion)");
    QTest::newRow("quoted named version")
        << QStringLiteral(
               "The Champs - Tequila [\"I Don't Want to Sing Karaoke\" Version] (Funbox Karaoke, 1958)")
        << QStringLiteral(
               "The Champs - Tequila (\"I Don't Want to Sing Karaoke\") (1958)");
    QTest::newRow("cursed version")
        << QStringLiteral(
               "Darude - Sandstorm (Cursed Version) (Funbox Karaoke, 2000)")
        << QStringLiteral("Darude - Sandstorm (Cursed) (2000)");
    QTest::newRow("solo version")
        << QStringLiteral(
               "Ernest Tubb - Too Old To Cut The Mustard (Solo Version) (Karaoke)")
        << QStringLiteral("Ernest Tubb - Too Old To Cut The Mustard (Solo)");
    QTest::newRow("compound extended version")
        << QStringLiteral(
               "Faithless - Insomnia [Monster Mix - Extended Version] (Funbox Karaoke, 1995)")
        << QStringLiteral(
               "Faithless - Insomnia (Monster Mix - Extended) (1995)");
    QTest::newRow("obskure best karaoke version")
        << QStringLiteral(
               "Imagine Dragons - Believer - Best Karaoke Version - Instrumental Lyrics - ObsKure")
        << QStringLiteral("Imagine Dragons - Believer");
    QTest::newRow("obskure instrumental lyrics without karaoke marker")
        << QStringLiteral(
               "Mazzy Star - Into Dust - Instrumental Lyrics - ObsKure")
        << QStringLiteral("Mazzy Star - Into Dust");
    QTest::newRow("one music")
        << QStringLiteral("karaoke THOMAS ARYA - BUNGA (ACOUSTIC VERSION)")
        << QStringLiteral("Thomas Arya - Bunga (Acoustic)");
    QTest::newRow("one music trailing marker")
        << QStringLiteral("SALIF KEITA - FOLON karaoke")
        << QStringLiteral("Salif Keita - Folon");
    QTest::newRow("janet email")
        << QStringLiteral("The Beaches — Lesbian of the Year (Karaoke)")
        << QStringLiteral("The Beaches - Lesbian of the Year");
    QTest::newRow("couch potato")
        << QStringLiteral("Nirvana - Spank Thru - Karaoke")
        << QStringLiteral("Nirvana - Spank Thru");
    QTest::newRow("couch potato middle marker")
        << QStringLiteral("Codefendants - Karaoke - The Right Wrong Man")
        << QStringLiteral("Codefendants - The Right Wrong Man");
    QTest::newRow("karaoke nerds")
        << QStringLiteral("The Films - Bodybag (Karaoke)")
        << QStringLiteral("The Films - Bodybag");
    QTest::newRow("peareoke")
        << QStringLiteral("The Lemonheads - Fear of Living (Karaoke)")
        << QStringLiteral("The Lemonheads - Fear of Living");
    QTest::newRow("peareoke year")
        << QStringLiteral("Aesop Rock - Citronella (Karaoke) 2007")
        << QStringLiteral("Aesop Rock - Citronella (2007)");
    QTest::newRow("peareoke karaoke duet")
        << QStringLiteral("The Proclaimers - Over and Done With (Karaoke Duet)")
        << QStringLiteral("The Proclaimers - Over and Done With (Duet)");
    QTest::newRow("peareoke solo karaoke")
        << QStringLiteral("Unicorns - I was Born A Unicorn (Solo Karaoke)")
        << QStringLiteral("Unicorns - I was Born A Unicorn (Solo)");
    QTest::newRow("cc karaoke modern")
        << QStringLiteral("Pink Floyd • Comfortably Numb (CC Karaoke / Instrumental)")
        << QStringLiteral("Pink Floyd - Comfortably Numb");
    QTest::newRow("cc karaoke trailing uvr")
        << QStringLiteral("Radiohead • Let Down (CC Karaoke / Instrumental) [UVR]")
        << QStringLiteral("Radiohead - Let Down");
    QTest::newRow("cc karaoke leading uvr")
        << QStringLiteral("Green Day - Christie Road [UVR] (CC Karaoke / Instrumental)")
        << QStringLiteral("Green Day - Christie Road");
    QTest::newRow("cc karaoke legacy")
        << QStringLiteral("Phoebe Bridgers - I Know The End [CC] [Karaoke Instrumental]")
        << QStringLiteral("Phoebe Bridgers - I Know The End");
    QTest::newRow("cc karaoke legacy version")
        << QStringLiteral(
               "Arctic Monkeys - Old Yellow Bricks [CC] [Karaoke Instrumental Version]")
        << QStringLiteral("Arctic Monkeys - Old Yellow Bricks");
    QTest::newRow("cc karaoke decorated legacy")
        << QStringLiteral(
               "Michael Jackson - Bad (CC) 🎤 [Karaoke] [Instrumental]")
        << QStringLiteral("Michael Jackson - Bad");
    QTest::newRow("cc karaoke early bullet")
        << QStringLiteral("Panic! At The Disco • The Only Difference (Karaoke)")
        << QStringLiteral("Panic! At The Disco - The Only Difference");
    QTest::newRow("leading contextual tag")
        << QStringLiteral("(Funbox Karaoke, 2004) Artist - Song")
        << QStringLiteral("Artist - Song");
    QTest::newRow("leading game tag and brackets")
        << QStringLiteral(
               "(Sonic Adventure 2) Hunnid-P - A Ghost's Pumpkin Soup [Pumpkin Hill Theme] (Funbox Karaoke, 2001)")
        << QStringLiteral(
               "Hunnid-P - A Ghost's Pumpkin Soup (Pumpkin Hill Theme) (2001)");
    QTest::newRow("bracketed artist remains")
        << QStringLiteral("[spunge] - Lyrical Content (Funbox Karaoke, 1999)")
        << QStringLiteral("(spunge) - Lyrical Content (1999)");
    QTest::newRow("quoted weird al")
        << QStringLiteral("\"Weird Al\" Yankovic - Harvey The Wonder Hamster (Karaoke)")
        << QStringLiteral("Weird Al Yankovic - Harvey The Wonder Hamster");
    QTest::newRow("artist featuring credit")
        << QStringLiteral("Artist Featuring Guest - Song")
        << QStringLiteral("Artist Ft. Guest - Song");
    QTest::newRow("parenthesized artist feature credit")
        << QStringLiteral("Artist (Feat. Guest) - Song")
        << QStringLiteral("Artist Ft. Guest - Song");
    QTest::newRow("misplaced title feature credit")
        << QStringLiteral("Artist - Song ft Guest")
        << QStringLiteral("Artist Ft. Guest - Song");
    QTest::newRow("parenthesized misplaced title feature credit")
        << QStringLiteral("Artist - Song (Featuring Guest) (Live)")
        << QStringLiteral("Artist Ft. Guest - Song (Live)");
    QTest::newRow("misplaced title feature after malformed karaoke marker")
        << QStringLiteral(
               "Coolio - Gangsta's Paradise feat. L.V. (Karaoke Version0")
        << QStringLiteral("Coolio Ft. L.V. - Gangsta's Paradise");
    QTest::newRow("featured credit inside title qualifier")
        << QStringLiteral(
               "Kasino - Can't Get Over (Live Sabadaço - Ft. Gilberto Barros)")
        << QStringLiteral(
               "Kasino Ft. Gilberto Barros - Can't Get Over (Live Sabadaço)");
    QTest::newRow("unrelated suffix")
        << QStringLiteral("  Artist - Song (Live, 2004)  ")
        << QStringLiteral("Artist - Song (Live, 2004)");
    QTest::newRow("unbranded best version remains")
        << QStringLiteral("Artist - Song - Best Version")
        << QStringLiteral("Artist - Song - Best Version");
}

void KaraokeBackendTest::cleanedTitle()
{
    QFETCH(QString, raw);
    QFETCH(QString, expected);
    QCOMPARE(KaraokeBackend::cleanedTitle(raw), expected);
}

void KaraokeBackendTest::cleansProviderTitles()
{
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("The Veils - Time (Karaoke Version; A Major)"),
            QStringLiteral("offbeat_karaoke")),
        QStringLiteral("The Veils - Time"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Dear Nora - Out to Dry (karaoke)"),
            QStringLiteral("karaoke_arr")),
        QStringLiteral("Dear Nora - Out to Dry"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Crystal Castles - Vanished (Karaoke)"),
            QStringLiteral("nicky_dee_karaoke")),
        QStringLiteral("Crystal Castles - Vanished"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Trentemoller - Feat. Sune Rose Wagner-Deceive"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("Trentemoller Ft. Sune Rose Wagner - Deceive"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("DABEULL - Feat. HOLYBRUNE - DX7"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("DABEULL Ft. HOLYBRUNE - DX7"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Cigarettes After Sex - Apocalypse (Karaoke Version)"),
            QStringLiteral("karaoke_office")),
        QStringLiteral("Cigarettes After Sex - Apocalypse"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Wiz Khalifa   See You Again ft  Charlie Puth Karaoke Version"),
            QStringLiteral("karaoke_office")),
        QStringLiteral("Wiz Khalifa Ft. Charlie Puth - See You Again"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "I Bet on Losing Dogs - Mitski (Karaoke Version)"),
            QStringLiteral("karaoke_office")),
        QStringLiteral("Mitski - I Bet on Losing Dogs"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("28 - Zach Bryan (Karaoke Version)"),
            QStringLiteral("karaoke_office")),
        QStringLiteral("Zach Bryan - 28"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Chris Brown - Run Away (Karaoke Version) ft. Bryson Tiller"),
            QStringLiteral("karaoke_office")),
        QStringLiteral("Chris Brown Ft. Bryson Tiller - Run Away"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Lady Gaga — Bad Romance • KARAOKE"),
            QStringLiteral("balka_karaoke")),
        QStringLiteral("Lady Gaga - Bad Romance"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("The Strokes - The Modern Age [karaoke]"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("The Strokes - The Modern Age"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "\"The Wicker Man\" written and performed by Pants"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Pants - The Wicker Man"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "\"Eye of the Tiger\" karaoke with animal sounds instead of instruments"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Survivor - Eye of the Tiger (Animal Sounds)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Survivor - Eye of the Tiger (barnyard animal remix) [karaoke]"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Survivor - Eye of the Tiger (Animal Sounds)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "\"High Blood Pressure\" (\"Under Pressure\" cover) at SD Downtown Throwdown (Geezer feat. Scotty Pants)"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral(
            "Geezer Ft. Scotty Pants - High Blood Pressure (Under Pressure Cover)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("\"I Sucked Off A Dude\" performed by Gerry"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Gerry - I Sucked Off A Dude"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("\"Meme\" by Ryan Bradford (Lorde parody)"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Ryan Bradford - Meme (Lorde Parody)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "\"Psychopath\" by Scotty Pants (Katy Perry parody)"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Scotty Pants - Psychopath (Katy Perry Parody)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("\"Psychopath\" performed by Renita"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Renita - Psychopath"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Velvet Underground  - Beginning to See the Light    (karaoke) 1969 The Velvet Underground Live"),
            QStringLiteral("lemmy_caution_karaoke")),
        QStringLiteral(
            "Velvet Underground - Beginning to See the Light (Live) (1969)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "I Wonder What's Inside Your Butthole [karaoke version of \"Elvis Costello\" version]"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("Jo Lee - I Wonder What's Inside Your Butthole"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "R.E.M. - Drive [karaoke version of the live version from the 'Monster' tour]"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral("R.E.M. - Drive (Monster Tour Live)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "David Bowie - Love is Lost (Pants Karaoke version of Hello Steve Reich Mix by James Murphy for DFA)"),
            QStringLiteral("pants_karaoke")),
        QStringLiteral(
            "David Bowie - Love is Lost (Hello Steve Reich Mix by James Murphy for DFA)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Austra, Avalon Emerson - Anywayz (Avalon Emerson's 14th Life Version; Karaoke Version; E Minor)"),
            QStringLiteral("offbeat_karaoke")),
        QStringLiteral(
            "Austra, Avalon Emerson - Anywayz (Avalon Emerson's 14th Life)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Gala - Freed From Desire (2024 Version; Diplo Edit; Karaoke Version)"),
            QStringLiteral("offbeat_karaoke")),
        QStringLiteral("Gala - Freed From Desire (Diplo Edit) (2024)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Violent Femmes - Color Me Once (Karaoke Version: Key Dbm)"),
            QStringLiteral("offbeat_karaoke")),
        QStringLiteral("Violent Femmes - Color Me Once"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "CAKE - Italian Leather Sofa; Key of E (Karaoke Version)"),
            QStringLiteral("offbeat_karaoke")),
        QStringLiteral("CAKE - Italian Leather Sofa"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("AHA - The Living Daylights (Key of D) (Karaoke Version)"),
            QStringLiteral("offbeat_karaoke")),
        QStringLiteral("AHA - The Living Daylights"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Rilo Kiley - The Good That Wont Come Out (Karaoke Version; Key Ab)"),
            QStringLiteral("offbeat_karaoke")),
        QStringLiteral("Rilo Kiley - The Good That Wont Come Out"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Gold - Sigur Rós - Instrumental - Karaoke"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("Sigur Rós - Gold"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Space Song - Beach House - Karaoke - Instrumental"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("Beach House - Space Song"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Invisible Thread - The Divine Comedy - Instrumental - Karaoke (new single)"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("The Divine Comedy - Invisible Thread (new single)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "HOODIEBLACK (Version symphonique)- Dejak - Karaoke - Instrumental"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("Dejak - HOODIEBLACK (Symphonique)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Bela Lugosis Dead (Official Version) - Bauhaus - Karaoke - Instrumental"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("Bauhaus - Bela Lugosi's Dead (Official)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Alone - The Cure - Instrumental Version - Karaoke Lyrics"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("The Cure - Alone"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "All I Ever Am - The Cure - Instrumental version - Karaoke Lyrics"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("The Cure - All I Ever Am"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "The Smashing Pumpkins - 1979 - Karaoke - Instrumental"),
            QStringLiteral("jlo_instru")),
        QStringLiteral("The Smashing Pumpkins - 1979"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("\"MusicKaraoke\" RICHARD HAWLEY - OPEN UP YOUR DOOR"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("Richard Hawley - Open Up Your Door"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "(VOCAL REMOVE) SALIF KEITA & CESARIA EVORA -YAMORE... MusicKaraoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("Salif Keita & Cesaria Evora - Yamore"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("KARAOKE - BRMC-BEAT THE DEVILS TATTOO-MusicKaraoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("BRMC - Beat The Devils Tattoo"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "2PAC - TUPAC SHAKUR - 2 OF AMERICAZ MOST WANTED karaoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("2Pac - 2 Of Americaz Most Wanted"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("2PAC-TUPAC SHAKUR-ONLY GOD CAN JUDGE ME karaoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("2Pac - Only God Can Judge Me"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("2PAC-TUPAC SHAKUR - BRENDAS GOT A BABY karaoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("2Pac - Brendas Got A Baby"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "2PAC-TUPAC SHAKUR-WONDA WHY THEY CALL U BITCH karaoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("2Pac - Wonda Why They Call U Bitch"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "AMBER RUN - I FOUND - (Instrumental version)karaoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("Amber Run - I Found"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "extended version MADONNA-FORBIDDEN LOVE karaoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("Madonna - Forbidden Love (Extended)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "XRINA THE SMITHS THE BOY WITH THE THORN IN HIS SIDE"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("The Smiths - The Boy With The Thorn In His Side"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "XRINA THE SMITHS LAST NIGHT I DREAMT THAT SOMEBODY LOVED ME"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral(
            "The Smiths - Last Night I Dreamt That Somebody Loved Me"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("XRINA VELVET UNDERGROUND   FEMME FATALE"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("Velvet Underground - Femme Fatale"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "karaoke XRINA GRAFFITI6 FREE MusicKaraoke"),
            QStringLiteral("one_music_karaoke")),
        QStringLiteral("Graffiti6 Free"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "All Time Low - Dear Maria, Count Me In [CC] [Karaoke Version]"),
            QStringLiteral("cc_karaoke_x")),
        QStringLiteral("All Time Low - Dear Maria, Count Me In"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "All Time Low - Dear Maria, Count Me In (CC) (Karaoke Version)"),
            QStringLiteral("cc_karaoke_x")),
        QStringLiteral("All Time Low - Dear Maria, Count Me In"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "talking heads - psycho killer (karaoke) stop making sense version"),
            QStringLiteral("lemmy_caution_karaoke")),
        QStringLiteral("talking heads - psycho killer (Stop Making Sense)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "talking heads - houses in motion (karaoke) live version"),
            QStringLiteral("lemmy_caution_karaoke")),
        QStringLiteral("talking heads - houses in motion (Live)"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("45 Grave - Partytime Zombie version (karaoke)"),
            QStringLiteral("lemmy_caution_karaoke")),
        QStringLiteral("45 Grave - Partytime (Zombie)"));

    const QStringList lemmyVersionTitles{
        QStringLiteral("45 Grave - Partytime Zombie version (karaoke)"),
        QStringLiteral("Kanye West & Lil Pump - I Love It (karaoke) SNL version"),
        QStringLiteral(
            "david bowie - bring me the disco king (karaoke) underworld soundtrack version"),
        QStringLiteral("Eric B & Rakim - Paid In Full (karaoke) album version"),
        QStringLiteral("Kane Brown - Heaven (karaoke) piano version"),
        QStringLiteral(
            "stevie nicks - wild heart (karaoke) rolling stone photoshoot version"),
        QStringLiteral(
            "talking heads - heaven (karaoke) stop making sense version"),
        QStringLiteral(
            "talking heads - houses in motion (karaoke) live version"),
        QStringLiteral(
            "talking heads - psycho killer (karaoke) stop making sense version"),
        QStringLiteral("Aretha Franklin - Think (karaoke) Blues Brothers Version"),
        QStringLiteral("Cat Power - Naked If I Want To (karaoke) Jukebox Version"),
        QStringLiteral(
            "Elliott Smith - Miss Misery (karaoke) good will hunting version"),
        QStringLiteral(
            "Jacksons - Shake Your Body Down To The Ground (karaoke) Special Disco Version"),
        QStringLiteral(
            "Kid Cudi ft MGMT - Pursuit Of Happiness (karaoke) original version"),
        QStringLiteral("Lil Nas X - Old Town Road (karaoke) original version"),
        QStringLiteral("Underworld - Born Slippy (karaoke) short version"),
        QStringLiteral("Taylor Swift – All Too Well (karaoke) 10 Minute Version"),
        QStringLiteral("David Bowie - John, I'm Only Dancing (karaoke) sax version"),
        QStringLiteral("david bowie - rebel rebel (karaoke) US single version"),
        QStringLiteral(
            "New Order - Bizarre Love Triangle (karaoke) video version"),
        QStringLiteral("Barbra Streisand - My man (karaoke) movie version")
    };
    for (const QString &rawTitle : lemmyVersionTitles) {
        const QString displayTitle = KaraokeBackend::cleanedTitle(
            rawTitle, QStringLiteral("lemmy_caution_karaoke"));
        QVERIFY2(!displayTitle.endsWith(QStringLiteral(" Version"),
                                        Qt::CaseInsensitive),
                 qPrintable(rawTitle + QStringLiteral(" => ") + displayTitle));
    }

    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("The Strokes - Dine N'Dash (Karaoke version)"),
            QStringLiteral("just_sing_karaoke")),
        QStringLiteral("The Strokes - Dine N'Dash"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "The Voidz - Leave It In My Dreams (High Quality Karaoke version)"),
            QStringLiteral("just_sing_karaoke")),
        QStringLiteral("The Voidz - Leave It In My Dreams"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Titãs - Epitáfio (Karaoke alta qualidade)"),
            QStringLiteral("just_sing_karaoke")),
        QStringLiteral("Titãs - Epitáfio"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral("Shalon Israel - Cabeça de Gelo (Karaoke com letra)"),
            QStringLiteral("just_sing_karaoke")),
        QStringLiteral("Shalon Israel - Cabeça de Gelo"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "You Oughta Know - Alanis Morissette | Karaoke Version | KaraFun"),
            QStringLiteral("karafun_karaoke")),
        QStringLiteral("Alanis Morissette - You Oughta Know"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Alanis Morissette - You Oughta Know (Karaoke Version)"),
            QStringLiteral("karafun_karaoke")),
        QStringLiteral("Alanis Morissette - You Oughta Know"));
    QCOMPARE(
        KaraokeBackend::cleanedTitle(
            QStringLiteral(
                "Karaoke Father Figure (MTV Unplugged) - George Michael *"),
            QStringLiteral("karafun_karaoke")),
        QStringLiteral("George Michael - Father Figure (MTV Unplugged)"));

    QCOMPARE(KaraokeBackend::cleanedTitle(
                 QStringLiteral("Artist - Song - Karaoke - Instrumental")),
             QStringLiteral("Artist - Song - Instrumental"));
}

void KaraokeBackendTest::validatesCatalogSongs()
{
    const QJsonObject valid{
        {QStringLiteral("id"), QStringLiteral("abcDEF123_-")},
        {QStringLiteral("title"), QStringLiteral("Artist - Song (Funbox Karaoke, 2004)")},
        {QStringLiteral("channel_id"), QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g")},
        {QStringLiteral("duration"), 187}
    };
    const QVariantMap song = KaraokeBackend::catalogSongFromJson(valid);
    QCOMPARE(song.value(QStringLiteral("videoId")).toString(), QStringLiteral("abcDEF123_-"));
    QCOMPARE(song.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Song (2004)"));
    QCOMPARE(song.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://www.youtube.com/watch?v=abcDEF123_-"));
    QCOMPARE(song.value(QStringLiteral("durationSeconds")).toInt(), 187);
    QCOMPARE(song.value(QStringLiteral("sourceId")).toString(), QStringLiteral("funbox"));

    const QJsonObject obsKure{
        {QStringLiteral("id"), QStringLiteral("ZDa00TaH8pw")},
        {QStringLiteral("title"),
         QStringLiteral("OneRepublic - Kids - Karaoke Instrumental Lyrics - ObsKure")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCkXE7x417ME2iudNECaLUFA")},
        {QStringLiteral("duration"), 239}
    };
    const QVariantMap obsKureSong = KaraokeBackend::catalogSongFromJson(obsKure);
    QCOMPARE(obsKureSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("OneRepublic - Kids"));
    QCOMPARE(obsKureSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("obskure"));

    const QJsonObject karaokeNerds{
        {QStringLiteral("id"), QStringLiteral("1QXAxXl8p-E")},
        {QStringLiteral("title"), QStringLiteral("The Films - Bodybag (Karaoke)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCBfV298JqKc8o9CM0aANz5A")}
    };
    const QVariantMap karaokeNerdsSong = KaraokeBackend::catalogSongFromJson(karaokeNerds);
    QCOMPARE(karaokeNerdsSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Films - Bodybag"));
    QCOMPARE(karaokeNerdsSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("karaoke_nerds"));

    const QJsonObject jloInstru{
        {QStringLiteral("id"), QStringLiteral("xMVI_7V3thg")},
        {QStringLiteral("title"),
         QStringLiteral("Gold - Sigur Rós - Instrumental - Karaoke")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ")}
    };
    const QVariantMap jloInstruSong = KaraokeBackend::catalogSongFromJson(jloInstru);
    QCOMPARE(jloInstruSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Sigur Rós - Gold"));
    QCOMPARE(jloInstruSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("jlo_instru"));

    const QJsonObject offbeatKaraoke{
        {QStringLiteral("id"), QStringLiteral("UuVJzffyvP8")},
        {QStringLiteral("title"),
         QStringLiteral("The Veils - Time (Karaoke Version; A Major)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCIauImhx1GGrl7LubRCxXcg")}
    };
    const QVariantMap offbeatSong = KaraokeBackend::catalogSongFromJson(
        offbeatKaraoke);
    QCOMPARE(offbeatSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Veils - Time"));
    QCOMPARE(offbeatSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("offbeat_karaoke"));

    const QJsonObject peareoke{
        {QStringLiteral("id"), QStringLiteral("mHRWdhAtwsk")},
        {QStringLiteral("title"),
         QStringLiteral("The Lemonheads - Fear of Living (Karaoke)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ")}
    };
    const QVariantMap peareokeSong = KaraokeBackend::catalogSongFromJson(peareoke);
    QCOMPARE(peareokeSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Lemonheads - Fear of Living"));
    QCOMPARE(peareokeSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("peareoke"));

    const QJsonObject karaokeOffice{
        {QStringLiteral("id"), QStringLiteral("K6lnji_t4Co")},
        {QStringLiteral("title"),
         QStringLiteral("Cigarettes After Sex - Apocalypse (Karaoke Version)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCR0kPElUivbuZC7Myr7Tg1Q")}
    };
    const QVariantMap karaokeOfficeSong =
        KaraokeBackend::catalogSongFromJson(karaokeOffice);
    QCOMPARE(karaokeOfficeSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Cigarettes After Sex - Apocalypse"));
    QCOMPARE(karaokeOfficeSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("karaoke_office"));

    const QJsonObject invertedKaraokeOffice{
        {QStringLiteral("id"), QStringLiteral("csqS6Dcu0_A")},
        {QStringLiteral("title"),
         QStringLiteral("28 - Zach Bryan (Karaoke Version)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCR0kPElUivbuZC7Myr7Tg1Q")}
    };
    QCOMPARE(
        KaraokeBackend::catalogSongFromJson(invertedKaraokeOffice)
            .value(QStringLiteral("displayTitle")).toString(),
        QStringLiteral("Zach Bryan - 28"));

    const QJsonObject ccKaraoke{
        {QStringLiteral("id"), QStringLiteral("nZqyaiL4fNg")},
        {QStringLiteral("title"),
         QStringLiteral("Pink Floyd • Comfortably Numb (CC Karaoke / Instrumental)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg")}
    };
    const QVariantMap ccKaraokeSong = KaraokeBackend::catalogSongFromJson(ccKaraoke);
    QCOMPARE(ccKaraokeSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Pink Floyd - Comfortably Numb"));
    QCOMPARE(ccKaraokeSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("cc_karaoke_x"));

    const QJsonObject pantsKaraoke{
        {QStringLiteral("id"), QStringLiteral("feXSlGC8JWM")},
        {QStringLiteral("title"),
         QStringLiteral("The Strokes - The Modern Age [karaoke]")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog")}
    };
    const QVariantMap pantsSong = KaraokeBackend::catalogSongFromJson(pantsKaraoke);
    QCOMPARE(pantsSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Strokes - The Modern Age"));
    QCOMPARE(pantsSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("pants_karaoke"));

    const QJsonObject unattributedPantsParody{
        {QStringLiteral("id"), QStringLiteral("trFzmVZ1Q0o")},
        {QStringLiteral("title"),
         QStringLiteral(
             "25 Minutes or Less (karaoke parody of \"25 Minutes to Go\")")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog")}
    };
    QVERIFY(KaraokeBackend::catalogSongFromJson(unattributedPantsParody).isEmpty());

    const QJsonObject karaokeArr{
        {QStringLiteral("id"), QStringLiteral("LnS1UYRBMl8")},
        {QStringLiteral("title"), QStringLiteral("Dear Nora - Out to Dry (karaoke)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCvgYvYeZe-BANj-cVUd59mQ")}
    };
    const QVariantMap karaokeArrSong = KaraokeBackend::catalogSongFromJson(karaokeArr);
    QCOMPARE(karaokeArrSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Dear Nora - Out to Dry"));
    QCOMPARE(karaokeArrSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("karaoke_arr"));

    const QJsonObject nickyDeeKaraoke{
        {QStringLiteral("id"), QStringLiteral("NFreUxLHj-I")},
        {QStringLiteral("title"),
         QStringLiteral("Crystal Castles - Vanished (Karaoke)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UC6m4V2RfKXs4dP3R7AfCK4g")}
    };
    const QVariantMap nickyDeeSong =
        KaraokeBackend::catalogSongFromJson(nickyDeeKaraoke);
    QCOMPARE(nickyDeeSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Crystal Castles - Vanished"));
    QCOMPARE(nickyDeeSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("nicky_dee_karaoke"));

    const QJsonObject balkaKaraoke{
        {QStringLiteral("id"), QStringLiteral("egOF3gFxcdo")},
        {QStringLiteral("title"),
         QStringLiteral("Lady Gaga — Bad Romance • KARAOKE")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCqspiYXbxpZpgWzzxUUbTiw")}
    };
    const QVariantMap balkaSong =
        KaraokeBackend::catalogSongFromJson(balkaKaraoke);
    QCOMPARE(balkaSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Lady Gaga - Bad Romance"));
    QCOMPARE(balkaSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("balka_karaoke"));

    const QJsonObject oneMusic{
        {QStringLiteral("id"), QStringLiteral("Ia7iVK8r7zY")},
        {QStringLiteral("title"),
         QStringLiteral("karaoke THOMAS ARYA - BUNGA (ACOUSTIC VERSION)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg")}
    };
    const QVariantMap oneMusicSong = KaraokeBackend::catalogSongFromJson(oneMusic);
    QCOMPARE(oneMusicSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Thomas Arya - Bunga (Acoustic)"));
    QCOMPARE(oneMusicSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("one_music_karaoke"));

    const QJsonObject janetEmail{
        {QStringLiteral("id"), QStringLiteral("GDO0mmhLb0c")},
        {QStringLiteral("title"),
         QStringLiteral("The Beaches — Lesbian of the Year (Karaoke)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw")}
    };
    const QVariantMap janetEmailSong = KaraokeBackend::catalogSongFromJson(janetEmail);
    QCOMPARE(janetEmailSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Beaches - Lesbian of the Year"));
    QCOMPARE(janetEmailSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("janet_email_karaoke"));

    const QJsonObject couchPotato{
        {QStringLiteral("id"), QStringLiteral("wD15O2SpU_k")},
        {QStringLiteral("title"), QStringLiteral("Nirvana - Spank Thru - Karaoke")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg")}
    };
    const QVariantMap couchPotatoSong = KaraokeBackend::catalogSongFromJson(couchPotato);
    QCOMPARE(couchPotatoSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Nirvana - Spank Thru"));
    QCOMPARE(couchPotatoSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("couch_potato_karaoke"));

    const QJsonObject lemmyCaution{
        {QStringLiteral("id"), QStringLiteral("0uiDCPL7_vY")},
        {QStringLiteral("title"),
         QStringLiteral("Carly Simon – You Belong to Me (karaoke)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ")}
    };
    const QVariantMap lemmyCautionSong = KaraokeBackend::catalogSongFromJson(
        lemmyCaution);
    QCOMPARE(lemmyCautionSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Carly Simon – You Belong to Me"));
    QCOMPARE(lemmyCautionSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("lemmy_caution_karaoke"));

    const QJsonObject justSingKaraoke{
        {QStringLiteral("id"), QStringLiteral("9p4otqDAXQs")},
        {QStringLiteral("title"),
         QStringLiteral("The Strokes - Dine N'Dash (Karaoke version)")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCM8kkIU5aIzCbyZawksZ2Bw")}
    };
    const QVariantMap justSingSong = KaraokeBackend::catalogSongFromJson(
        justSingKaraoke);
    QCOMPARE(justSingSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Strokes - Dine N'Dash"));
    QCOMPARE(justSingSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("just_sing_karaoke"));

    const QJsonObject karaFun{
        {QStringLiteral("id"), QStringLiteral("0BKshfVAS4Q")},
        {QStringLiteral("title"),
         QStringLiteral(
             "You Oughta Know - Alanis Morissette | Karaoke Version | KaraFun")},
        {QStringLiteral("playlist_channel_id"),
         QStringLiteral("UCbqcG1rdt9LMwOJN4PyGTKg")}
    };
    const QVariantMap karaFunSong = KaraokeBackend::catalogSongFromJson(karaFun);
    QCOMPARE(karaFunSong.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Alanis Morissette - You Oughta Know"));
    QCOMPARE(karaFunSong.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("karafun_karaoke"));

    QJsonObject artistFirstKaraFun = karaFun;
    artistFirstKaraFun[QStringLiteral("id")] = QStringLiteral("KaraFun0001");
    artistFirstKaraFun[QStringLiteral("title")] = QStringLiteral(
        "Alanis Morissette - You Oughta Know (Karaoke Version)");
    QCOMPARE(KaraokeBackend::catalogSongFromJson(artistFirstKaraFun)
                 .value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Alanis Morissette - You Oughta Know"));

    QJsonObject legacyKaraFun = karaFun;
    legacyKaraFun[QStringLiteral("id")] = QStringLiteral("KaraFun0002");
    legacyKaraFun[QStringLiteral("title")] = QStringLiteral(
        "Karaoke Father Figure (MTV Unplugged) - George Michael *");
    QCOMPARE(KaraokeBackend::catalogSongFromJson(legacyKaraFun)
                 .value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("George Michael - Father Figure (MTV Unplugged)"));

    QJsonObject karaFunPromo = karaFun;
    karaFunPromo[QStringLiteral("id")] = QStringLiteral("PromoVid001");
    karaFunPromo[QStringLiteral("title")] =
        QStringLiteral("Discover the KaraFun subscription");
    QVERIFY(KaraokeBackend::catalogSongFromJson(karaFunPromo).isEmpty());

    QJsonObject wrongChannel = valid;
    wrongChannel[QStringLiteral("channel_id")] = QStringLiteral("wrong-channel");
    QVERIFY(KaraokeBackend::catalogSongFromJson(wrongChannel).isEmpty());

    QJsonObject invalidId = valid;
    invalidId[QStringLiteral("id")] = QStringLiteral("too-short");
    QVERIFY(KaraokeBackend::catalogSongFromJson(invalidId).isEmpty());
}

void KaraokeBackendTest::deduplicatesCatalogBySourcePriority()
{
    QCOMPARE(
        KaraokeBackend::deduplicationKey(
            QStringLiteral("The Mars Vólta - The Widow (Funbox Karaoke, 2005)")),
        KaraokeBackend::deduplicationKey(
            QStringLiteral("Mars Volta - Widow - Karaoke Instrumental Lyrics - ObsKure")));
    QCOMPARE(
        KaraokeBackend::deduplicationKey(QStringLiteral("Beyoncé & The Band - A Song")),
        KaraokeBackend::deduplicationKey(QStringLiteral("Beyonce and Band - Song")));
    QCOMPARE(
        KaraokeBackend::deduplicationKey(
            QStringLiteral(
                "Austra, Avalon Emerson - Anywayz (Avalon Emerson's 14th Life Version)")),
        KaraokeBackend::deduplicationKey(
            QStringLiteral(
                "Austra & Avalon Emerson - Anywayz (Avalon Emersons 14th Life Version)")));

    const auto song = [](const QString &videoId, const QString &title,
                         const QString &sourceId) {
        return QVariantMap{
            {QStringLiteral("videoId"), videoId},
            {QStringLiteral("rawTitle"), title},
            {QStringLiteral("displayTitle"), title},
            {QStringLiteral("sourceId"), sourceId}
        };
    };

    const QStringList sourceIds{
        QStringLiteral("funbox"),
        QStringLiteral("karaoke_nerds"),
        QStringLiteral("jlo_instru"),
        QStringLiteral("offbeat_karaoke"),
        QStringLiteral("peareoke"),
        QStringLiteral("karaoke_office"),
        QStringLiteral("cc_karaoke_x"),
        QStringLiteral("nicky_dee_karaoke"),
        QStringLiteral("balka_karaoke"),
        QStringLiteral("pants_karaoke"),
        QStringLiteral("karaoke_arr"),
        QStringLiteral("obskure"),
        QStringLiteral("one_music_karaoke"),
        QStringLiteral("janet_email_karaoke"),
        QStringLiteral("couch_potato_karaoke"),
        QStringLiteral("lemmy_caution_karaoke"),
        QStringLiteral("just_sing_karaoke"),
        QStringLiteral("karafun_karaoke")
    };
    QVariantList candidates;
    for (int winningPriority = 0; winningPriority < sourceIds.size(); ++winningPriority) {
        const QString title = QStringLiteral("Artist - Priority %1").arg(winningPriority);
        for (int priority = sourceIds.size() - 1;
             priority >= winningPriority; --priority) {
            const QChar sourceMarker = QChar::fromLatin1(
                static_cast<char>('A' + priority));
            const QString videoId = QString(sourceMarker)
                                    + QString::number(winningPriority)
                                          .rightJustified(10, QLatin1Char('0'));
            candidates.append(song(videoId, title, sourceIds.at(priority)));
        }
    }
    candidates.append(song(QStringLiteral("Z0000000000"),
                           QStringLiteral("Artist - Priority 0"),
                           QStringLiteral("funbox")));

    const QVariantList result = KaraokeBackend::deduplicatedCatalog(candidates);
    QCOMPARE(result.size(), sourceIds.size());
    for (int priority = 0; priority < sourceIds.size(); ++priority) {
        const QString key = KaraokeBackend::deduplicationKey(
            QStringLiteral("Artist - Priority %1").arg(priority));
        const auto selected = std::find_if(
            result.cbegin(), result.cend(), [&key](const QVariant &candidate) {
                return KaraokeBackend::deduplicationKey(
                    candidate.toMap().value(QStringLiteral("displayTitle")).toString()) == key;
            });
        QVERIFY(selected != result.cend());
        QCOMPARE(selected->toMap().value(QStringLiteral("sourceId")).toString(),
                 sourceIds.at(priority));
    }

    const QVariantList austraDuplicates{
        song(QStringLiteral("gbx-PCbA_Fg"),
             QStringLiteral(
                 "Austra, Avalon Emerson - Anywayz (Avalon Emerson's 14th Life Version)"),
             QStringLiteral("offbeat_karaoke")),
        song(QStringLiteral("L9TOHoYvq98"),
             QStringLiteral(
                 "Austra & Avalon Emerson - Anywayz (Avalon Emersons 14th Life Version)"),
             QStringLiteral("offbeat_karaoke"))
    };
    const QVariantList deduplicatedAustra =
        KaraokeBackend::deduplicatedCatalog(austraDuplicates);
    QCOMPARE(deduplicatedAustra.size(), 1);
    QCOMPARE(deduplicatedAustra.first().toMap().value(
                 QStringLiteral("videoId")).toString(),
             QStringLiteral("gbx-PCbA_Fg"));

    const QString animalSoundsTitle = KaraokeBackend::cleanedTitle(
        QStringLiteral(
            "\"Eye of the Tiger\" karaoke with animal sounds instead of instruments"),
        QStringLiteral("pants_karaoke"));
    const QString barnyardRemixTitle = KaraokeBackend::cleanedTitle(
        QStringLiteral(
            "Survivor - Eye of the Tiger (barnyard animal remix) [karaoke]"),
        QStringLiteral("pants_karaoke"));
    const QVariantList pantsAnimalDuplicates{
        song(QStringLiteral("JZJvOabcCgg"), animalSoundsTitle,
             QStringLiteral("pants_karaoke")),
        song(QStringLiteral("G5l0ot8p-Iw"), barnyardRemixTitle,
             QStringLiteral("pants_karaoke"))
    };
    QCOMPARE(KaraokeBackend::deduplicatedCatalog(pantsAnimalDuplicates).size(), 1);
}

void KaraokeBackendTest::validatesCatalogSizes()
{
    QVERIFY(!KaraokeBackend::isPlausibleCatalogSize(99, 0));
    QVERIFY(KaraokeBackend::isPlausibleCatalogSize(100, 0));
    QVERIFY(KaraokeBackend::isPlausibleCatalogSize(2576, 2862));
    QVERIFY(!KaraokeBackend::isPlausibleCatalogSize(2575, 2862));
    QVERIFY(!KaraokeBackend::isPlausibleCatalogSize(49, 0, 50));
    QVERIFY(KaraokeBackend::isPlausibleCatalogSize(78, 0, 50));
    QVERIFY(!KaraokeBackend::isPlausibleCatalogSize(78, 0));
    QVERIFY(KaraokeBackend::isPlausibleCatalogSize(71, 78, 50));
    QVERIFY(!KaraokeBackend::isPlausibleCatalogSize(70, 78, 50));
}

void KaraokeBackendTest::resolvesPinnedYoutubePlaybackArguments()
{
    const QString appRoot = QStringLiteral("/missing-app-root");
    const QString ytDlp = HelperResolver::ytDlp(appRoot);
    const QString deno = HelperResolver::deno(appRoot);
    const QStringList arguments = HelperResolver::youtubeMpvArguments(appRoot);

    QVERIFY(!ytDlp.isEmpty());
    QVERIFY(!deno.isEmpty());
    QVERIFY(!HelperResolver::ffmpeg(appRoot).isEmpty());
    QVERIFY(arguments.contains(QStringLiteral("--no-config")));
    QVERIFY(arguments.contains(
        QStringLiteral("--script-opts-append=ytdl_hook-ytdl_path=") + ytDlp));
    QVERIFY(arguments.contains(
        QStringLiteral("--ytdl-raw-options-append=js-runtimes=deno:") + deno));
    QVERIFY(arguments.contains(
        QStringLiteral("--ytdl-raw-options-append=ignore-config=")));
    QVERIFY(arguments.contains(
        QStringLiteral("--ytdl-raw-options-append=no-update=")));
}

void KaraokeBackendTest::usesCachedPlaybackMedia()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());

    const QString videoId = QStringLiteral("abcDEF123_-");
    QVERIFY(backend.enqueue({
        {QStringLiteral("videoId"), videoId},
        {QStringLiteral("rawTitle"), QStringLiteral("Artist - Cached Song")}
    }));

    const QString cacheDirectory = dataRoot.filePath(QStringLiteral("karaoke_playback_cache"));
    QVERIFY(QDir().mkpath(cacheDirectory));
    const QString mediaPath = QDir(cacheDirectory).filePath(videoId + QStringLiteral(".mp4"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    QCOMPARE(media.write(QByteArray(2048, 'x')), qint64(2048));
    media.close();

    QCOMPARE(backend.cachedPlaybackPath(videoId), QFileInfo(mediaPath).canonicalFilePath());
    const QString playlistPath = backend.writePlaybackPlaylist();
    QVERIFY(!playlistPath.isEmpty());
    QFile playlist(playlistPath);
    QVERIFY(playlist.open(QIODevice::ReadOnly));
    const QByteArray contents = playlist.readAll();
    QVERIFY(contents.contains(mediaPath.toUtf8()));
    QVERIFY(!contents.contains("youtube.com"));
}

void KaraokeBackendTest::loadsFreshCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cachedSong{
        {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
        {QStringLiteral("rawTitle"),
         QStringLiteral("Artist - Cached Song (Funbox Karaoke, 2004)")},
        {QStringLiteral("displayTitle"), QStringLiteral("untrusted cached display title")},
        {QStringLiteral("url"), QStringLiteral("https://example.invalid/untrusted")}
    };
    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("channelId"), QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g")},
        {QStringLiteral("fetchedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{cachedSong}}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0, true);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 1);
    const QVariantMap loaded = items.first().toMap();
    QCOMPARE(loaded.value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Cached Song (2004)"));
    QCOMPARE(loaded.value(QStringLiteral("url")).toString(),
             QStringLiteral("https://www.youtube.com/watch?v=abcDEF123_-"));
    QCOMPARE(loaded.value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("funbox"));

    const QVariantMap search = backend.searchCatalog(QStringLiteral("cached song"), 0, 25);
    QCOMPARE(search.value(QStringLiteral("total")).toInt(), 1);
    QCOMPARE(search.value(QStringLiteral("offset")).toInt(), 0);
    const QVariantList searchItems = search.value(QStringLiteral("items")).toList();
    QCOMPARE(searchItems.size(), 1);
    QCOMPARE(searchItems.first().toMap().value(QStringLiteral("videoId")).toString(),
             QStringLiteral("abcDEF123_-"));
}

void KaraokeBackendTest::loadsLegacyMultiSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1},
            {QStringLiteral("obskure"), 1}
        }},
        {QStringLiteral("fetchedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("Funbox00001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Funbox Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("ObsKure0001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - ObsKure Song - Karaoke Instrumental Lyrics - ObsKure")},
                {QStringLiteral("sourceId"), QStringLiteral("obskure")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Funbox Song (2004)"));
    QCOMPARE(items.at(1).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - ObsKure Song"));
}

void KaraokeBackendTest::loadsLegacyFourSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 3},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1},
            {QStringLiteral("karaoke_nerds"), 1},
            {QStringLiteral("peareoke"), 1},
            {QStringLiteral("obskure"), 1}
        }},
        {QStringLiteral("fetchedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("Funbox00001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Funbox Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("1QXAxXl8p-E")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Films - Bodybag (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("karaoke_nerds")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("mHRWdhAtwsk")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Lemonheads - Fear of Living (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("peareoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("ZDa00TaH8pw")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("OneRepublic - Kids - Karaoke Instrumental Lyrics - ObsKure")},
                {QStringLiteral("sourceId"), QStringLiteral("obskure")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 4);
    QCOMPARE(items.at(0).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Funbox Song (2004)"));
    QCOMPARE(items.at(1).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Films - Bodybag"));
    QCOMPARE(items.at(2).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Lemonheads - Fear of Living"));
    QCOMPARE(items.at(3).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("OneRepublic - Kids"));
}

void KaraokeBackendTest::loadsLegacyFiveSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 4},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1},
            {QStringLiteral("karaoke_nerds"), 1},
            {QStringLiteral("peareoke"), 1},
            {QStringLiteral("cc_karaoke_x"), 1},
            {QStringLiteral("obskure"), 1}
        }},
        {QStringLiteral("fetchedAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("Funbox00001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Funbox Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("1QXAxXl8p-E")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Films - Bodybag (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("karaoke_nerds")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("mHRWdhAtwsk")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Lemonheads - Fear of Living (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("peareoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("nZqyaiL4fNg")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Pink Floyd • Comfortably Numb (CC Karaoke / Instrumental)")},
                {QStringLiteral("sourceId"), QStringLiteral("cc_karaoke_x")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("ZDa00TaH8pw")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("OneRepublic - Kids - Karaoke Instrumental Lyrics - ObsKure")},
                {QStringLiteral("sourceId"), QStringLiteral("obskure")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 5);
    QCOMPARE(items.at(0).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Funbox Song (2004)"));
    QCOMPARE(items.at(1).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Films - Bodybag"));
    QCOMPARE(items.at(2).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Lemonheads - Fear of Living"));
    QCOMPARE(items.at(3).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Pink Floyd - Comfortably Numb"));
    QCOMPARE(items.at(4).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("OneRepublic - Kids"));
}

void KaraokeBackendTest::loadsLegacySixSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 5},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1},
            {QStringLiteral("karaoke_nerds"), 1},
            {QStringLiteral("peareoke"), 1},
            {QStringLiteral("cc_karaoke_x"), 1},
            {QStringLiteral("obskure"), 1},
            {QStringLiteral("lemmy_caution_karaoke"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("Funbox00001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Funbox Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("1QXAxXl8p-E")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Films - Bodybag (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("karaoke_nerds")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("mHRWdhAtwsk")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Lemonheads - Fear of Living (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("peareoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("nZqyaiL4fNg")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Pink Floyd • Comfortably Numb (CC Karaoke / Instrumental)")},
                {QStringLiteral("sourceId"), QStringLiteral("cc_karaoke_x")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("ZDa00TaH8pw")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("OneRepublic - Kids - Karaoke Instrumental Lyrics - ObsKure")},
                {QStringLiteral("sourceId"), QStringLiteral("obskure")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("0uiDCPL7_vY")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Carly Simon – You Belong to Me (karaoke)")},
                {QStringLiteral("sourceId"),
                 QStringLiteral("lemmy_caution_karaoke")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 6);
    QCOMPARE(items.at(0).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Funbox Song (2004)"));
    QCOMPARE(items.at(1).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Films - Bodybag"));
    QCOMPARE(items.at(2).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Lemonheads - Fear of Living"));
    QCOMPARE(items.at(3).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Pink Floyd - Comfortably Numb"));
    QCOMPARE(items.at(4).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("OneRepublic - Kids"));
    QCOMPARE(items.at(5).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Carly Simon – You Belong to Me"));
}

void KaraokeBackendTest::loadsLegacyNineSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 6},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
            QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
            QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1},
            {QStringLiteral("karaoke_nerds"), 1},
            {QStringLiteral("peareoke"), 1},
            {QStringLiteral("cc_karaoke_x"), 1},
            {QStringLiteral("obskure"), 1},
            {QStringLiteral("one_music_karaoke"), 1},
            {QStringLiteral("janet_email_karaoke"), 1},
            {QStringLiteral("couch_potato_karaoke"), 1},
            {QStringLiteral("lemmy_caution_karaoke"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("Funbox00001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Funbox Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("1QXAxXl8p-E")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Films - Bodybag (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("karaoke_nerds")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("mHRWdhAtwsk")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Lemonheads - Fear of Living (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("peareoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("nZqyaiL4fNg")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Pink Floyd • Comfortably Numb (CC Karaoke / Instrumental)")},
                {QStringLiteral("sourceId"), QStringLiteral("cc_karaoke_x")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("ZDa00TaH8pw")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("OneRepublic - Kids - Karaoke Instrumental Lyrics - ObsKure")},
                {QStringLiteral("sourceId"), QStringLiteral("obskure")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("Ia7iVK8r7zY")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("karaoke THOMAS ARYA - BUNGA (ACOUSTIC VERSION)")},
                {QStringLiteral("sourceId"), QStringLiteral("one_music_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("GDO0mmhLb0c")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Beaches — Lesbian of the Year (Karaoke)")},
                {QStringLiteral("sourceId"), QStringLiteral("janet_email_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("wD15O2SpU_k")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Nirvana - Spank Thru - Karaoke")},
                {QStringLiteral("sourceId"), QStringLiteral("couch_potato_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("0uiDCPL7_vY")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Carly Simon – You Belong to Me (karaoke)")},
                {QStringLiteral("sourceId"),
                 QStringLiteral("lemmy_caution_karaoke")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 9);
    const QStringList expectedTitles{
        QStringLiteral("Artist - Funbox Song (2004)"),
        QStringLiteral("The Films - Bodybag"),
        QStringLiteral("The Lemonheads - Fear of Living"),
        QStringLiteral("Pink Floyd - Comfortably Numb"),
        QStringLiteral("OneRepublic - Kids"),
        QStringLiteral("Thomas Arya - Bunga (Acoustic)"),
        QStringLiteral("The Beaches - Lesbian of the Year"),
        QStringLiteral("Nirvana - Spank Thru"),
        QStringLiteral("Carly Simon – You Belong to Me")
    };
    for (int index = 0; index < expectedTitles.size(); ++index) {
        QCOMPARE(items.at(index).toMap().value(QStringLiteral("displayTitle")).toString(),
                 expectedTitles.at(index));
    }
}

void KaraokeBackendTest::loadsLegacyElevenSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 7},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ"),
            QStringLiteral("UCIauImhx1GGrl7LubRCxXcg"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
            QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
            QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Song (2004)"));
}

void KaraokeBackendTest::loadsLegacyThirteenSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 8},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ"),
            QStringLiteral("UCIauImhx1GGrl7LubRCxXcg"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog"),
            QStringLiteral("UCvgYvYeZe-BANj-cVUd59mQ"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
            QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
            QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Song (2004)"));
}

void KaraokeBackendTest::loadsLegacyFifteenSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 9},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ"),
            QStringLiteral("UCIauImhx1GGrl7LubRCxXcg"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UC6m4V2RfKXs4dP3R7AfCK4g"),
            QStringLiteral("UCqspiYXbxpZpgWzzxUUbTiw"),
            QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog"),
            QStringLiteral("UCvgYvYeZe-BANj-cVUd59mQ"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
            QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
            QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Song (2004)"));
}

void KaraokeBackendTest::loadsLegacySixteenSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 10},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ"),
            QStringLiteral("UCIauImhx1GGrl7LubRCxXcg"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCR0kPElUivbuZC7Myr7Tg1Q"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog"),
            QStringLiteral("UCvgYvYeZe-BANj-cVUd59mQ"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UC6m4V2RfKXs4dP3R7AfCK4g"),
            QStringLiteral("UCqspiYXbxpZpgWzzxUUbTiw"),
            QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
            QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
            QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Song (2004)"));
}

void KaraokeBackendTest::loadsLegacySeventeenSourceCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 11},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ"),
            QStringLiteral("UCIauImhx1GGrl7LubRCxXcg"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCR0kPElUivbuZC7Myr7Tg1Q"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog"),
            QStringLiteral("UCvgYvYeZe-BANj-cVUd59mQ"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UC6m4V2RfKXs4dP3R7AfCK4g"),
            QStringLiteral("UCqspiYXbxpZpgWzzxUUbTiw"),
            QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
            QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
            QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ"),
            QStringLiteral("UCM8kkIU5aIzCbyZawksZ2Bw")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("funbox"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Artist - Song (Funbox Karaoke, 2004)")},
                {QStringLiteral("sourceId"), QStringLiteral("funbox")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Artist - Song (2004)"));
}

void KaraokeBackendTest::loadsCurrentCatalogCache()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());

    const QJsonObject cache{
        {QStringLiteral("schemaVersion"), 12},
        {QStringLiteral("sourceChannelIds"), QJsonArray{
            QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
            QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
            QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ"),
            QStringLiteral("UCIauImhx1GGrl7LubRCxXcg"),
            QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
            QStringLiteral("UCR0kPElUivbuZC7Myr7Tg1Q"),
            QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
            QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog"),
            QStringLiteral("UCvgYvYeZe-BANj-cVUd59mQ"),
            QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
            QStringLiteral("UC6m4V2RfKXs4dP3R7AfCK4g"),
            QStringLiteral("UCqspiYXbxpZpgWzzxUUbTiw"),
            QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
            QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
            QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
            QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ"),
            QStringLiteral("UCM8kkIU5aIzCbyZawksZ2Bw"),
            QStringLiteral("UCbqcG1rdt9LMwOJN4PyGTKg")
        }},
        {QStringLiteral("sourceItemCounts"), QJsonObject{
            {QStringLiteral("jlo_instru"), 2},
            {QStringLiteral("offbeat_karaoke"), 1},
            {QStringLiteral("karaoke_office"), 1},
            {QStringLiteral("cc_karaoke_x"), 1},
            {QStringLiteral("pants_karaoke"), 1},
            {QStringLiteral("karaoke_arr"), 1},
            {QStringLiteral("nicky_dee_karaoke"), 1},
            {QStringLiteral("balka_karaoke"), 1},
            {QStringLiteral("one_music_karaoke"), 2},
            {QStringLiteral("lemmy_caution_karaoke"), 1},
            {QStringLiteral("just_sing_karaoke"), 1},
            {QStringLiteral("karafun_karaoke"), 1}
        }},
        {QStringLiteral("fetchedAt"),
         QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
        {QStringLiteral("items"), QJsonArray{
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("xMVI_7V3thg")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Gold - Sigur Rós - Instrumental - Karaoke")},
                {QStringLiteral("sourceId"), QStringLiteral("jlo_instru")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("QVABupBPT_E")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "All I Ever Am - The Cure - Instrumental version - Karaoke Lyrics")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached jlo title")},
                {QStringLiteral("sourceId"), QStringLiteral("jlo_instru")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("UuVJzffyvP8")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Veils - Time (Karaoke Version; A Major)")},
                {QStringLiteral("sourceId"), QStringLiteral("offbeat_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("K6lnji_t4Co")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "Cigarettes After Sex - Apocalypse (Karaoke Version)")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached karaoke office title")},
                {QStringLiteral("sourceId"), QStringLiteral("karaoke_office")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("UiQpliZkLY0")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("\"MusicKaraoke\" RICHARD HAWLEY - OPEN UP YOUR DOOR")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached one music title")},
                {QStringLiteral("sourceId"), QStringLiteral("one_music_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("0A4fxw53yKA")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "AMBER RUN - I FOUND - (Instrumental version)karaoke")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached one music instrumental title")},
                {QStringLiteral("sourceId"), QStringLiteral("one_music_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("vWQo0otxfvQ")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "All Time Low - Dear Maria, Count Me In [CC] [Karaoke Version]")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached cc title")},
                {QStringLiteral("sourceId"), QStringLiteral("cc_karaoke_x")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("feXSlGC8JWM")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("The Strokes - The Modern Age [karaoke]")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached pants title")},
                {QStringLiteral("sourceId"), QStringLiteral("pants_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("RupL-XxT0vA")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "\"The Wicker Man\" written and performed by Pants")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached pants credit title")},
                {QStringLiteral("sourceId"), QStringLiteral("pants_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("LnS1UYRBMl8")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Dear Nora - Out to Dry (karaoke)")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached karaoke arr title")},
                {QStringLiteral("sourceId"), QStringLiteral("karaoke_arr")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("NFreUxLHj-I")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Crystal Castles - Vanished (Karaoke)")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached nicky dee title")},
                {QStringLiteral("sourceId"), QStringLiteral("nicky_dee_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("egOF3gFxcdo")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Lady Gaga — Bad Romance • KARAOKE")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached balka title")},
                {QStringLiteral("sourceId"), QStringLiteral("balka_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("FkN5cqxRGNA")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "talking heads - psycho killer (karaoke) stop making sense version")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached lemmy title")},
                {QStringLiteral("sourceId"),
                 QStringLiteral("lemmy_caution_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("JustSing001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Titãs - Epitáfio (Karaoke alta qualidade)")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached just sing title")},
                {QStringLiteral("sourceId"), QStringLiteral("just_sing_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("0BKshfVAS4Q")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "You Oughta Know - Alanis Morissette | Karaoke Version | KaraFun")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached karafun title")},
                {QStringLiteral("sourceId"), QStringLiteral("karafun_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("PromoVid001")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral("Discover the KaraFun subscription")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached karafun promo")},
                {QStringLiteral("sourceId"), QStringLiteral("karafun_karaoke")}
            },
            QJsonObject{
                {QStringLiteral("videoId"), QStringLiteral("trFzmVZ1Q0o")},
                {QStringLiteral("rawTitle"),
                 QStringLiteral(
                     "25 Minutes or Less (karaoke parody of \"25 Minutes to Go\")")},
                {QStringLiteral("displayTitle"),
                 QStringLiteral("stale cached unattributed parody")},
                {QStringLiteral("sourceId"), QStringLiteral("pants_karaoke")}
            }
        }}
    };
    QFile file(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QVERIFY(file.write(QJsonDocument(cache).toJson(QJsonDocument::Compact)) > 0);
    file.close();

    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    backend.loadCatalog();

    QCOMPARE(resetSpy.size(), 1);
    QCOMPARE(finishedSpy.size(), 1);
    const QVariantList items = resetSpy.first().at(0).toList();
    QCOMPARE(items.size(), 15);
    QCOMPARE(items.at(0).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Sigur Rós - Gold"));
    QCOMPARE(items.at(1).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Cure - All I Ever Am"));
    QCOMPARE(items.at(2).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Veils - Time"));
    QCOMPARE(items.at(3).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Cigarettes After Sex - Apocalypse"));
    QCOMPARE(items.at(4).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Richard Hawley - Open Up Your Door"));
    QCOMPARE(items.at(5).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Amber Run - I Found"));
    QCOMPARE(items.at(6).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("All Time Low - Dear Maria, Count Me In"));
    QCOMPARE(items.at(7).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("The Strokes - The Modern Age"));
    QCOMPARE(items.at(8).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Pants - The Wicker Man"));
    QCOMPARE(items.at(9).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Dear Nora - Out to Dry"));
    QCOMPARE(items.at(10).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Crystal Castles - Vanished"));
    QCOMPARE(items.at(11).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Lady Gaga - Bad Romance"));
    QCOMPARE(items.at(12).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("talking heads - psycho killer (Stop Making Sense)"));
    QCOMPARE(items.at(13).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Titãs - Epitáfio"));
    QCOMPARE(items.at(14).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Alanis Morissette - You Oughta Know"));
}

void KaraokeBackendTest::persistsAndMutatesQueue()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());
    QSignalSpy changedSpy(&backend, &KaraokeBackend::queueChanged);

    const QVariantMap first{
        {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
        {QStringLiteral("rawTitle"),
         QStringLiteral("Gold - Sigur Rós - Instrumental - Karaoke")},
        {QStringLiteral("sourceId"), QStringLiteral("jlo_instru")}
    };
    const QVariantMap second{
        {QStringLiteral("videoId"), QStringLiteral("ZyxWvu987_0")},
        {QStringLiteral("rawTitle"), QStringLiteral("Artist - Second (Funbox Karaoke, 2005)")}
    };

    QVERIFY(backend.enqueue(first));
    QVERIFY(backend.enqueue(first));
    QVERIFY(backend.enqueue(second));
    QCOMPARE(changedSpy.size(), 3);

    QVariantList queue = backend.getQueue();
    QCOMPARE(queue.size(), 3);
    const QString firstEntryId = queue.at(0).toMap().value(QStringLiteral("entryId")).toString();
    const QString duplicateEntryId = queue.at(1).toMap().value(QStringLiteral("entryId")).toString();
    QVERIFY(!firstEntryId.isEmpty());
    QVERIFY(firstEntryId != duplicateEntryId);
    QCOMPARE(queue.at(0).toMap().value(QStringLiteral("displayTitle")).toString(),
             QStringLiteral("Sigur Rós - Gold"));
    QCOMPARE(queue.at(0).toMap().value(QStringLiteral("sourceId")).toString(),
             QStringLiteral("jlo_instru"));

    QVERIFY(backend.moveQueueEntry(2, 0));
    queue = backend.getQueue();
    QCOMPARE(queue.at(0).toMap().value(QStringLiteral("videoId")).toString(),
             QStringLiteral("ZyxWvu987_0"));

    QVERIFY(backend.failQueueEntry(
        duplicateEntryId,
        QStringLiteral("network error at https://example.invalid/private?token=secret")));
    queue = backend.getQueue();
    const int failedIndex = queue.at(1).toMap().value(QStringLiteral("entryId")).toString()
                          == duplicateEntryId ? 1 : 2;
    QCOMPARE(queue.at(failedIndex).toMap().value(QStringLiteral("status")).toString(),
             QStringLiteral("failed"));
    QVERIFY(!queue.at(failedIndex).toMap().value(QStringLiteral("error")).toString()
                 .contains(QStringLiteral("example.invalid")));
    QVERIFY(backend.resetQueueEntry(duplicateEntryId));

    const QString playlistPath = backend.writePlaybackPlaylist();
    QVERIFY(!playlistPath.isEmpty());
    QFile playlist(playlistPath);
    QVERIFY(playlist.open(QIODevice::ReadOnly));
    const QByteArray playlistContents = playlist.readAll();
    QVERIFY(playlistContents.startsWith("#EXTM3U\n"));
    QCOMPARE(playlistContents.count("https://www.youtube.com/watch?v=abcDEF123_-"), 2);
    QCOMPARE(playlistContents.count("https://www.youtube.com/watch?v=ZyxWvu987_0"), 1);
    QVERIFY(!playlistContents.contains("example.invalid"));

    {
        KaraokeBackend reloaded(QStringLiteral("/missing-app-root"), dataRoot.path());
        QCOMPARE(reloaded.getQueue(), backend.getQueue());
    }

    QVERIFY(backend.completeQueueEntry(firstEntryId));
    QCOMPARE(backend.getQueue().size(), 2);
    backend.clearQueueExcept(duplicateEntryId);
    QCOMPARE(backend.getQueue().size(), 1);
    QCOMPARE(backend.getQueue().first().toMap().value(QStringLiteral("entryId")).toString(),
             duplicateEntryId);
    backend.clearQueue();
    QVERIFY(backend.getQueue().isEmpty());
}

void KaraokeBackendTest::rejectsInvalidQueueEntries()
{
    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    KaraokeBackend backend(QStringLiteral("/missing-app-root"), dataRoot.path());

    QVERIFY(!backend.enqueue({
        {QStringLiteral("videoId"), QStringLiteral("invalid")},
        {QStringLiteral("rawTitle"), QStringLiteral("Artist - Song")}
    }));
    QVERIFY(!backend.enqueue({
        {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
        {QStringLiteral("rawTitle"), QString{}}
    }));
    QVERIFY(backend.getQueue().isEmpty());
}

void KaraokeBackendTest::rollsBackQueueOnPersistenceFailure()
{
    QTemporaryFile notADirectory;
    QVERIFY(notADirectory.open());
    KaraokeBackend backend(QStringLiteral("/missing-app-root"), notADirectory.fileName());

    QVERIFY(!backend.enqueue({
        {QStringLiteral("videoId"), QStringLiteral("abcDEF123_-")},
        {QStringLiteral("rawTitle"), QStringLiteral("Artist - Song")}
    }));
    QVERIFY(backend.getQueue().isEmpty());
}

void KaraokeBackendTest::refreshesLiveCatalog()
{
    if (qEnvironmentVariable("KARAOKE_LIVE_TEST") != QStringLiteral("1"))
        QSKIP("Set KARAOKE_LIVE_TEST=1 to exercise the pinned helpers and live channels.");

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    KaraokeBackend backend(QCoreApplication::applicationDirPath(), dataRoot.path());
    QSignalSpy resetSpy(&backend, &KaraokeBackend::catalogReset);
    QSignalSpy appendSpy(&backend, &KaraokeBackend::catalogItemsAppended);
    QSignalSpy finishedSpy(&backend, &KaraokeBackend::catalogLoadFinished);
    QSignalSpy failedSpy(&backend, &KaraokeBackend::catalogLoadFailed);

    backend.refreshCatalog();
    QTRY_VERIFY_WITH_TIMEOUT(!finishedSpy.isEmpty() || !failedSpy.isEmpty(), 600000);
    QCOMPARE(failedSpy.size(), 0);
    QVERIFY(!resetSpy.isEmpty());
    QVERIFY(!appendSpy.isEmpty());
    QVERIFY(finishedSpy.last().at(0).toInt() > 100);
    QFile cache(dataRoot.filePath(QStringLiteral("karaoke_catalog.json")));
    QVERIFY(cache.open(QIODevice::ReadOnly));
    const QJsonDocument cacheDocument = QJsonDocument::fromJson(cache.readAll());
    QVERIFY(cacheDocument.isObject());
    QCOMPARE(cacheDocument.object().value(QStringLiteral("schemaVersion")).toInt(), 12);
    const QJsonObject sourceItemCounts = cacheDocument.object().value(
        QStringLiteral("sourceItemCounts")).toObject();
    QVERIFY(sourceItemCounts.value(QStringLiteral("funbox")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("karaoke_nerds")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("jlo_instru")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("offbeat_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("peareoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("karaoke_office")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("cc_karaoke_x")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("pants_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("karaoke_arr")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("obskure")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("nicky_dee_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("balka_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("one_music_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("janet_email_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("couch_potato_karaoke")).toInt() >= 50);
    QVERIFY(sourceItemCounts.value(
        QStringLiteral("lemmy_caution_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("just_sing_karaoke")).toInt() >= 100);
    QVERIFY(sourceItemCounts.value(QStringLiteral("karafun_karaoke")).toInt() >= 100);

    QHash<QString, int> visibleCounts;
    QHash<QString, QString> visibleSourceByKey;
    QStringList featuredCreditProblems;
    const QJsonArray items = cacheDocument.object().value(QStringLiteral("items")).toArray();
    for (const QJsonValue &value : items) {
        const QVariantMap item = value.toObject().toVariantMap();
        const QString sourceId = item.value(QStringLiteral("sourceId")).toString();
        ++visibleCounts[sourceId];
        const QString displayTitle = item.value(QStringLiteral("displayTitle")).toString();
        QVERIFY2(!item.value(QStringLiteral("rawTitle")).toString().startsWith(
                     QStringLiteral("25 Minutes or Less (karaoke parody"),
                     Qt::CaseInsensitive),
                 "unattributed Pants parody remained in the catalog");
        QVERIFY(!displayTitle.contains(QLatin1Char('[')));
        QVERIFY(!displayTitle.contains(QLatin1Char(']')));
        QVERIFY(!displayTitle.startsWith(QStringLiteral("(Sonic Adventure 2)"),
                                         Qt::CaseInsensitive));
        QVERIFY(!displayTitle.contains(QStringLiteral("\"Weird Al\" Yankovic"),
                                       Qt::CaseInsensitive));
        QVERIFY(!displayTitle.contains(QStringLiteral("7 inch version"),
                                       Qt::CaseInsensitive));
        QVERIFY(!displayTitle.contains(QStringLiteral("video version"),
                                       Qt::CaseInsensitive));
        static const QRegularExpression verboseEditionLabel(
            QStringLiteral("\\([^()\\r\\n]{1,120}\\s+Version\\)"),
            QRegularExpression::CaseInsensitiveOption);
        QVERIFY(!verboseEditionLabel.match(displayTitle).hasMatch());
        static const QRegularExpression leadingVersionLabel(
            QStringLiteral("\\(\\s*Version\\s+[^()\\r\\n]{1,120}\\)"),
            QRegularExpression::CaseInsensitiveOption);
        QVERIFY(!leadingVersionLabel.match(displayTitle).hasMatch());
        const qsizetype artistSeparator = displayTitle.indexOf(
            QStringLiteral(" - "));
        if (artistSeparator > 0) {
            static const QRegularExpression legacyArtistFeatureCredit(
                QStringLiteral(
                    "\\b(?:Featuring|Feat)\\.?(?=\\s)|\\bFt(?=\\s)"),
                QRegularExpression::CaseInsensitiveOption);
            static const QRegularExpression misplacedTitleFeatureCredit(
                QStringLiteral(
                    "\\b(?:Featuring|Feat|Ft)\\.?(?=\\s)"),
                QRegularExpression::CaseInsensitiveOption);
            if (legacyArtistFeatureCredit.match(
                    displayTitle.left(artistSeparator)).hasMatch()) {
                featuredCreditProblems.append(
                    QStringLiteral("legacy artist credit: ") + sourceId
                    + QStringLiteral(": ") + displayTitle);
            }
            if (misplacedTitleFeatureCredit.match(
                    displayTitle.mid(artistSeparator + 3)).hasMatch()) {
                featuredCreditProblems.append(
                    QStringLiteral("title-side credit: ") + sourceId
                    + QStringLiteral(": ") + displayTitle);
            }
        }
        if (sourceId == QStringLiteral("karaoke_nerds") ||
            sourceId == QStringLiteral("peareoke") ||
            sourceId == QStringLiteral("karaoke_office") ||
            sourceId == QStringLiteral("pants_karaoke") ||
            sourceId == QStringLiteral("karaoke_arr") ||
            sourceId == QStringLiteral("nicky_dee_karaoke") ||
            sourceId == QStringLiteral("balka_karaoke") ||
            sourceId == QStringLiteral("just_sing_karaoke") ||
            sourceId == QStringLiteral("karafun_karaoke")) {
            QVERIFY2(!displayTitle.endsWith(QStringLiteral("(Karaoke)"),
                                            Qt::CaseInsensitive),
                     qPrintable(sourceId + QStringLiteral(": ")
                                + displayTitle));
            QVERIFY(!displayTitle.endsWith(QStringLiteral(" - Karaoke"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.endsWith(QStringLiteral("(Karaoke Version)"),
                                           Qt::CaseInsensitive));
            if (sourceId == QStringLiteral("pants_karaoke")) {
                QVERIFY(!displayTitle.startsWith(QLatin1Char('"')));
                QVERIFY(!displayTitle.startsWith(QChar(0x201c)));
                QVERIFY(!displayTitle.contains(
                    QStringLiteral("Written and Performed by"),
                    Qt::CaseInsensitive));
                QVERIFY(!displayTitle.contains(
                    QStringLiteral("Karaoke Version of"),
                    Qt::CaseInsensitive));
                QVERIFY(!displayTitle.contains(
                    QStringLiteral("Pants Karaoke Version of"),
                    Qt::CaseInsensitive));
            }
            if (sourceId == QStringLiteral("just_sing_karaoke")) {
                QVERIFY(!displayTitle.contains(
                    QStringLiteral("High Quality Karaoke"),
                    Qt::CaseInsensitive));
                QVERIFY(!displayTitle.contains(
                    QStringLiteral("Karaoke alta qualidade"),
                    Qt::CaseInsensitive));
                QVERIFY(!displayTitle.contains(
                    QStringLiteral("Karaoke com letra"),
                    Qt::CaseInsensitive));
            }
            if (sourceId == QStringLiteral("karafun_karaoke")) {
                const QString rawTitle = item.value(
                    QStringLiteral("rawTitle")).toString();
                QVERIFY(rawTitle.endsWith(
                    QStringLiteral("| Karaoke Version | KaraFun"),
                    Qt::CaseInsensitive) ||
                        rawTitle.endsWith(
                            QStringLiteral("(Karaoke Version)"),
                            Qt::CaseInsensitive) ||
                        (rawTitle.startsWith(
                             QStringLiteral("Karaoke "),
                             Qt::CaseInsensitive) &&
                         rawTitle.endsWith(QLatin1Char('*'))));
                QVERIFY(!displayTitle.contains(
                    QStringLiteral("| KaraFun"),
                    Qt::CaseInsensitive));
            }
        } else if (sourceId == QStringLiteral("jlo_instru")) {
            static const QRegularExpression jloBranding(
                QStringLiteral(
                    "(?:Instrumental(?:\\s+Version)?\\s+-\\s+"
                    "Karaoke(?:\\s+Lyrics)?|"
                    "Karaoke\\s+-\\s+Instrumental(?:\\s+Version)?)"
                    "(?:\\s*\\([^()]+\\))?\\s*$"),
                QRegularExpression::CaseInsensitiveOption);
            QVERIFY(!jloBranding.match(displayTitle).hasMatch());
        } else if (sourceId == QStringLiteral("offbeat_karaoke")) {
            QVERIFY(!displayTitle.contains(QStringLiteral("Karaoke Version"),
                                           Qt::CaseInsensitive));
            static const QRegularExpression offbeatKeyMetadata(
                QStringLiteral(
                    "(?:;\\s*Key\\s+(?:of\\s+)?[A-G]|"
                    "\\(\\s*Key\\s+(?:of\\s+)?[A-G][^()]*\\))"),
                QRegularExpression::CaseInsensitiveOption);
            QVERIFY(!offbeatKeyMetadata.match(displayTitle).hasMatch());
        } else if (sourceId == QStringLiteral("cc_karaoke_x")) {
            QVERIFY(!displayTitle.contains(QStringLiteral("•")));
            QVERIFY(!displayTitle.contains(QStringLiteral("CC Karaoke"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("Karaoke Instrumental"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("Karaoke Version"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("(CC)"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("(Karaoke)"),
                                           Qt::CaseInsensitive));
        } else if (sourceId == QStringLiteral("obskure")) {
            QVERIFY(!displayTitle.contains(QStringLiteral("Karaoke Instrumental"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("Karaoke Version"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("Instrumental Lyrics"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.endsWith(QStringLiteral(" - Best Version"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.endsWith(QStringLiteral("(Best Version)"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.endsWith(QStringLiteral(" - Original Sound"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.endsWith(QStringLiteral(" - Som Original"),
                                           Qt::CaseInsensitive));
        } else if (sourceId == QStringLiteral("one_music_karaoke")) {
            QVERIFY(!displayTitle.startsWith(QStringLiteral("Xrina "),
                                             Qt::CaseInsensitive));
            QVERIFY(!displayTitle.startsWith(QStringLiteral("Xrina-"),
                                             Qt::CaseInsensitive));
            QVERIFY(!displayTitle.startsWith(QStringLiteral("Karaoke "),
                                             Qt::CaseInsensitive));
            QVERIFY(!displayTitle.endsWith(QStringLiteral(" Karaoke"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("MusicKaraoke"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("Vocal Remove"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("Instrumental Version"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.startsWith(QLatin1Char('-')));
            QVERIFY(!displayTitle.contains(
                QRegularExpression(
                    QStringLiteral("^2Pac\\s*-\\s*Tupac\\s+Shakur"),
                    QRegularExpression::CaseInsensitiveOption)));
        } else if (sourceId == QStringLiteral("janet_email_karaoke")) {
            QVERIFY(!displayTitle.contains(QStringLiteral("(Karaoke)"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.contains(QStringLiteral("—")));
        } else if (sourceId == QStringLiteral("couch_potato_karaoke")) {
            static const QRegularExpression couchKaraokeMarker(
                QStringLiteral("(?:^|\\s-\\s)Karaoke(?:\\s-\\s|\\s*\\(|$)"),
                QRegularExpression::CaseInsensitiveOption);
            QVERIFY(!couchKaraokeMarker.match(displayTitle).hasMatch());
        } else if (sourceId == QStringLiteral("lemmy_caution_karaoke")) {
            QVERIFY(!displayTitle.contains(QStringLiteral("(Karaoke)"),
                                           Qt::CaseInsensitive));
            QVERIFY(!displayTitle.endsWith(QStringLiteral(" Version"),
                                           Qt::CaseInsensitive));
        }
        const QString key = KaraokeBackend::deduplicationKey(displayTitle);
        if (key.isEmpty())
            continue;
        const auto existing = visibleSourceByKey.constFind(key);
        QVERIFY2(existing == visibleSourceByKey.cend(),
                 qPrintable(QStringLiteral("duplicate catalog title remained: ") + key));
        visibleSourceByKey[key] = sourceId;
    }
    QVERIFY2(featuredCreditProblems.isEmpty(),
             qPrintable(featuredCreditProblems.mid(0, 50).join(QLatin1Char('\n'))));
    QVERIFY(visibleCounts.value(QStringLiteral("funbox")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("karaoke_nerds")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("jlo_instru")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("offbeat_karaoke")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("peareoke")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("karaoke_office")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("cc_karaoke_x")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("pants_karaoke")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("karaoke_arr")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("obskure")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("nicky_dee_karaoke")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("balka_karaoke")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("one_music_karaoke")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("janet_email_karaoke")) >= 100);
    QVERIFY(visibleCounts.value(QStringLiteral("couch_potato_karaoke")) >= 50);
    QVERIFY(visibleCounts.value(QStringLiteral("lemmy_caution_karaoke")) >= 100);
    QCOMPARE(
        visibleSourceByKey.value(KaraokeBackend::deduplicationKey(
            QStringLiteral("Jo Lee - I Wonder What's Inside Your Butthole"))),
        QStringLiteral("pants_karaoke"));
    QCOMPARE(
        visibleSourceByKey.value(KaraokeBackend::deduplicationKey(
            QStringLiteral("Velvet Underground - Femme Fatale"))),
        QStringLiteral("one_music_karaoke"));
    QCOMPARE(
        visibleSourceByKey.value(KaraokeBackend::deduplicationKey(
            QStringLiteral("Crystal Castles - Vanished"))),
        QStringLiteral("nicky_dee_karaoke"));
    QCOMPARE(
        visibleSourceByKey.value(KaraokeBackend::deduplicationKey(
            QStringLiteral("Lady Gaga - Bad Romance"))),
        QStringLiteral("karaoke_office"));
    QCOMPARE(
        visibleSourceByKey.value(KaraokeBackend::deduplicationKey(
            QStringLiteral(
                "Velvet Underground - Beginning to See the Light (Live) (1969)"))),
        QStringLiteral("lemmy_caution_karaoke"));
    QVERIFY(visibleSourceByKey.contains(KaraokeBackend::deduplicationKey(
        QStringLiteral("Cigarettes After Sex - Apocalypse"))));
    QVERIFY(visibleSourceByKey.contains(KaraokeBackend::deduplicationKey(
        QStringLiteral("Wiz Khalifa Ft. Charlie Puth - See You Again"))));
    QVERIFY(visibleSourceByKey.contains(KaraokeBackend::deduplicationKey(
        QStringLiteral("Mitski - I Bet on Losing Dogs"))));
    QVERIFY(visibleSourceByKey.contains(KaraokeBackend::deduplicationKey(
        QStringLiteral("The Smashing Pumpkins - 1979"))));
    QVERIFY(visibleSourceByKey.contains(KaraokeBackend::deduplicationKey(
        QStringLiteral("Zach Bryan - 28"))));
}

void KaraokeBackendTest::prefetchesLivePlaybackMedia()
{
    if (qEnvironmentVariable("KARAOKE_LIVE_TEST") != QStringLiteral("1"))
        QSKIP("Set KARAOKE_LIVE_TEST=1 to exercise a complete prefetched song.");

    QTemporaryDir dataRoot;
    QVERIFY(dataRoot.isValid());
    KaraokeBackend backend(QCoreApplication::applicationDirPath(), dataRoot.path());
    QVERIFY(backend.enqueue({
        {QStringLiteral("videoId"), QStringLiteral("Ofh80EmEwRA")},
        {QStringLiteral("rawTitle"),
         QStringLiteral("Daft Punk - One More Time (Funbox Karaoke, 2000)")}
    }));

    const QString entryId = backend.getQueue().first().toMap()
                                .value(QStringLiteral("entryId")).toString();
    QSignalSpy readySpy(&backend, &KaraokeBackend::queueEntryPrefetched);
    QSignalSpy failedSpy(&backend, &KaraokeBackend::queueEntryPrefetchFailed);
    backend.prefetchQueueEntry(entryId);

    QTRY_VERIFY_WITH_TIMEOUT(!readySpy.isEmpty() || !failedSpy.isEmpty(), 120000);
    QCOMPARE(failedSpy.size(), 0);
    QCOMPARE(readySpy.last().at(0).toString(), entryId);
    const QString path = readySpy.last().at(1).toString();
    QVERIFY(QFileInfo(path).isFile());
    QVERIFY(QFileInfo(path).size() > 1024);
    QCOMPARE(backend.cachedPlaybackPath(QStringLiteral("Ofh80EmEwRA")), path);
}

int main(int argc, char **argv)
{
    // The exhaustive live refresh intentionally allows ten minutes across all
    // configured sources. QtTest otherwise aborts the function at its global
    // five-minute default before the test's explicit wait can finish.
    if (qEnvironmentVariable("KARAOKE_LIVE_TEST") == QLatin1String("1"))
        qputenv("QTEST_FUNCTION_TIMEOUT", QByteArrayLiteral("650000"));

    QCoreApplication application(argc, argv);
    KaraokeBackendTest testObject;
    return QTest::qExec(&testObject, argc, argv);
}

#include "KaraokeBackendTest.moc"
