#include "KaraokeBackend.h"

#include "tools/HelperResolver.h"
#include "tools/YouTubePolicy.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileInfoList>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUuid>

#include <utility>

#ifndef BUNDLED_YT_DLP_VERSION
#define BUNDLED_YT_DLP_VERSION "unknown"
#endif

namespace {

constexpr int kMinimumCatalogItems = 100;

struct CatalogSource {
    QString id;
    QString channelId;
    QString url;
    int introducedInSchema = 1;
    int minimumItems = kMinimumCatalogItems;
};

const QString kFunboxSourceId = QStringLiteral("funbox");
const QString kObsKureSourceId = QStringLiteral("obskure");
const QString kKaraokeNerdsSourceId = QStringLiteral("karaoke_nerds");
const QString kJLoInstruSourceId = QStringLiteral("jlo_instru");
const QString kOffbeatKaraokeSourceId = QStringLiteral("offbeat_karaoke");
const QString kPeareokeSourceId = QStringLiteral("peareoke");
const QString kKaraokeOfficeSourceId = QStringLiteral("karaoke_office");
const QString kCCKaraokeSourceId = QStringLiteral("cc_karaoke_x");
const QString kPantsKaraokeSourceId = QStringLiteral("pants_karaoke");
const QString kKaraokeArrSourceId = QStringLiteral("karaoke_arr");
const QString kNickyDeeKaraokeSourceId = QStringLiteral("nicky_dee_karaoke");
const QString kBalkaKaraokeSourceId = QStringLiteral("balka_karaoke");
const QString kLemmyCautionSourceId = QStringLiteral("lemmy_caution_karaoke");
const QString kOneMusicSourceId = QStringLiteral("one_music_karaoke");
const QString kJanetEmailSourceId = QStringLiteral("janet_email_karaoke");
const QString kCouchPotatoSourceId = QStringLiteral("couch_potato_karaoke");
const QString kJustSingKaraokeSourceId = QStringLiteral("just_sing_karaoke");
const QString kKaraFunSourceId = QStringLiteral("karafun_karaoke");
const QList<CatalogSource> kCatalogSources{
    {
        kFunboxSourceId,
        QStringLiteral("UCtPzvwooQ18YZ8Wq8Hka60g"),
        QStringLiteral("https://www.youtube.com/channel/UCtPzvwooQ18YZ8Wq8Hka60g/videos"),
        1
    },
    {
        kKaraokeNerdsSourceId,
        QStringLiteral("UCBfV298JqKc8o9CM0aANz5A"),
        QStringLiteral("https://www.youtube.com/channel/UCBfV298JqKc8o9CM0aANz5A/videos"),
        3
    },
    {
        kJLoInstruSourceId,
        QStringLiteral("UCoVB6wMm2pNGMKQTChKCiRQ"),
        QStringLiteral("https://www.youtube.com/channel/UCoVB6wMm2pNGMKQTChKCiRQ/videos"),
        7
    },
    {
        kOffbeatKaraokeSourceId,
        QStringLiteral("UCIauImhx1GGrl7LubRCxXcg"),
        QStringLiteral("https://www.youtube.com/channel/UCIauImhx1GGrl7LubRCxXcg/videos"),
        7
    },
    {
        kPeareokeSourceId,
        QStringLiteral("UCNU7LlZ_nKVaq9Lihj0sAHQ"),
        QStringLiteral("https://www.youtube.com/channel/UCNU7LlZ_nKVaq9Lihj0sAHQ/videos"),
        3
    },
    {
        kKaraokeOfficeSourceId,
        QStringLiteral("UCR0kPElUivbuZC7Myr7Tg1Q"),
        QStringLiteral("https://www.youtube.com/channel/UCR0kPElUivbuZC7Myr7Tg1Q/videos"),
        10
    },
    {
        kCCKaraokeSourceId,
        QStringLiteral("UCTQHT1Gj_D_Bc7P1REuMoAg"),
        QStringLiteral("https://www.youtube.com/channel/UCTQHT1Gj_D_Bc7P1REuMoAg/videos"),
        4
    },
    {
        kNickyDeeKaraokeSourceId,
        QStringLiteral("UC6m4V2RfKXs4dP3R7AfCK4g"),
        QStringLiteral("https://www.youtube.com/channel/UC6m4V2RfKXs4dP3R7AfCK4g/videos"),
        9
    },
    {
        kBalkaKaraokeSourceId,
        QStringLiteral("UCqspiYXbxpZpgWzzxUUbTiw"),
        QStringLiteral("https://www.youtube.com/channel/UCqspiYXbxpZpgWzzxUUbTiw/videos"),
        9
    },
    {
        kPantsKaraokeSourceId,
        QStringLiteral("UCPZsA3OSQreeZlKIo6jqUog"),
        QStringLiteral("https://www.youtube.com/channel/UCPZsA3OSQreeZlKIo6jqUog/videos"),
        8
    },
    {
        kKaraokeArrSourceId,
        QStringLiteral("UCvgYvYeZe-BANj-cVUd59mQ"),
        QStringLiteral("https://www.youtube.com/channel/UCvgYvYeZe-BANj-cVUd59mQ/videos"),
        8
    },
    {
        kObsKureSourceId,
        QStringLiteral("UCkXE7x417ME2iudNECaLUFA"),
        QStringLiteral("https://www.youtube.com/channel/UCkXE7x417ME2iudNECaLUFA/videos"),
        2
    },
    {
        kOneMusicSourceId,
        QStringLiteral("UCdwO61VZMYpozDiAJ6ZI3pg"),
        QStringLiteral("https://www.youtube.com/channel/UCdwO61VZMYpozDiAJ6ZI3pg/videos"),
        6
    },
    {
        kJanetEmailSourceId,
        QStringLiteral("UC4T6FfTdpvxUrf9-dd4kjpw"),
        QStringLiteral("https://www.youtube.com/channel/UC4T6FfTdpvxUrf9-dd4kjpw/videos"),
        6
    },
    {
        kCouchPotatoSourceId,
        QStringLiteral("UCxuk5azVGJ-aumAds7WMHmg"),
        QStringLiteral("https://www.youtube.com/channel/UCxuk5azVGJ-aumAds7WMHmg/videos"),
        6,
        50
    },
    {
        kLemmyCautionSourceId,
        QStringLiteral("UCg0i5aSL_2rhf4iztlLmLUQ"),
        QStringLiteral("https://www.youtube.com/channel/UCg0i5aSL_2rhf4iztlLmLUQ/videos"),
        5
    },
    {
        kJustSingKaraokeSourceId,
        QStringLiteral("UCM8kkIU5aIzCbyZawksZ2Bw"),
        QStringLiteral("https://www.youtube.com/channel/UCM8kkIU5aIzCbyZawksZ2Bw/videos"),
        11
    },
    {
        kKaraFunSourceId,
        QStringLiteral("UCbqcG1rdt9LMwOJN4PyGTKg"),
        QStringLiteral("https://www.youtube.com/channel/UCbqcG1rdt9LMwOJN4PyGTKg/videos"),
        12
    }
};
constexpr int kCatalogBatchSize = 256;
constexpr int kMaxHelperErrorBytes = 8192;
constexpr int kMaxCachedPlaybackFiles = 8;
constexpr qint64 kMaxCachedPlaybackBytes = 1024LL * 1024LL * 1024LL;

QString safeErrorText(const QByteArray &value)
{
    return YouTubePolicy::safeHelperError(value).left(600);
}

QString normalizedSearchText(QString text)
{
    text = text.toLower().normalized(QString::NormalizationForm_D);
    QString normalized;
    normalized.reserve(text.size());
    for (const QChar character : std::as_const(text)) {
        if (character.category() == QChar::Mark_NonSpacing)
            continue;
        switch (character.unicode()) {
        case 0x00E6: case 0x01FD: normalized += QStringLiteral("ae"); break;
        case 0x0153: normalized += QStringLiteral("oe"); break;
        case 0x00F8: normalized += QLatin1Char('o'); break;
        case 0x00F0: case 0x0111: normalized += QLatin1Char('d'); break;
        case 0x00FE: normalized += QStringLiteral("th"); break;
        case 0x00DF: normalized += QStringLiteral("ss"); break;
        case 0x0142: normalized += QLatin1Char('l'); break;
        case 0x0131: normalized += QLatin1Char('i'); break;
        default: normalized += character; break;
        }
    }
    return normalized;
}

QString articleInsensitiveSortKey(const QString &title)
{
    QString key = normalizedSearchText(title).trimmed();
    static const QRegularExpression article(
        QStringLiteral("^(?:a|an|the)\\s+"));
    key.remove(article);
    return key;
}

QString titleCasedIfAllCaps(QString title)
{
    bool hasLetter = false;
    for (const QChar character : std::as_const(title)) {
        if (!character.isLetter())
            continue;
        hasLetter = true;
        if (character.isLower())
            return title;
    }
    if (!hasLetter)
        return title;

    bool startsWord = true;
    for (QChar &character : title) {
        if (character.isLetter()) {
            character = startsWord ? character.toUpper() : character.toLower();
            startsWord = false;
        } else if (!character.isNumber()) {
            startsWord = true;
        }
    }
    return title;
}

QString titleCasedIfAllLowercase(QString title)
{
    bool hasLetter = false;
    for (const QChar character : std::as_const(title)) {
        if (!character.isLetter())
            continue;
        hasLetter = true;
        if (character.isUpper())
            return title;
    }
    if (!hasLetter)
        return title;

    bool startsWord = true;
    for (QChar &character : title) {
        if (character.isLetter()) {
            if (startsWord)
                character = character.toUpper();
            startsWord = false;
        } else if (!character.isNumber()) {
            startsWord = true;
        }
    }
    return title;
}

QString titleLookupKey(QString title)
{
    title = title.toCaseFolded();
    static const QRegularExpression punctuation(
        QStringLiteral("[^\\p{L}\\p{N}]+"));
    title.replace(punctuation, QStringLiteral(" "));
    return title.simplified();
}

QString withoutOneMusicBranding(QString title, bool *hadBranding)
{
    static const QList<QRegularExpression> brandingPatterns{
        QRegularExpression(
            QStringLiteral("^\\s*XRINA(?:\\s*[-:]\\s*|\\s+)"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(
                "\\s*(?:(?:Video\\s+)?by|made\\s+by|created\\s+by)?"
                "\\s*[\"“”]?MusicKaraoke[\"“”]?"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral("^\\s*\\(\\s*Vocal\\s+Remove(?:d)?\\s*\\)\\s*"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral("^\\s*(?:Karaoke|Training)\\s*(?::|-)?\\s*"),
            QRegularExpression::CaseInsensitiveOption),
        QRegularExpression(
            QStringLiteral(
                "\\s*-?\\s*\\(\\s*Instrumental\\s+Version\\s*\\)"
                "\\s*(?=Karaoke\\s*$)"),
            QRegularExpression::CaseInsensitiveOption)
    };
    bool removedBranding = false;
    do {
        removedBranding = false;
        for (const QRegularExpression &pattern : brandingPatterns) {
            if (!pattern.match(title).hasMatch())
                continue;
            *hadBranding = true;
            removedBranding = true;
            title.remove(pattern);
        }
    } while (removedBranding);

    static const QRegularExpression outerSeparators(
        QStringLiteral("(?:^\\s*[-:]+\\s*|\\s*[-:]+\\s*$)"));
    static const QRegularExpression trailingEllipsis(
        QStringLiteral("\\.{2,}\\s*$"));
    title.remove(outerSeparators);
    title.remove(trailingEllipsis);
    return title.trimmed();
}

QString normalizedOneMusicStructure(QString title)
{
    static const QRegularExpression featuredTupacAlias(
        QStringLiteral(
            "^\\s*2Pac\\s*-\\s*Tupac\\s+Shakur\\s+"
            "(Ft\\.?\\s+[^-]+)\\s*-\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tupacAlias(
        QStringLiteral(
            "^\\s*2Pac\\s*-\\s*Tupac\\s+Shakur\\s*-\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    if (featuredTupacAlias.match(title).hasMatch()) {
        title.replace(featuredTupacAlias, QStringLiteral("2Pac \\1 - "));
    } else {
        title.replace(tupacAlias, QStringLiteral("2Pac - "));
    }

    if (!title.contains(QRegularExpression(QStringLiteral("\\s+-\\s+")))) {
        static const QRegularExpression firstLegacySeparator(
            QStringLiteral("^([^-\\r\\n]{1,100})-(?=\\S)"));
        title.replace(firstLegacySeparator, QStringLiteral("\\1 - "));
    }
    if (!title.contains(QRegularExpression(QStringLiteral("\\s+-\\s+")))) {
        // A legacy XRINA batch omits the artist/title separator. Keep the
        // canonical artist prefixes in one extensible list and match longest
        // names first as more of that batch is identified.
        static const QStringList undelimitedArtistPrefixes{
            QStringLiteral("Velvet Underground"),
            QStringLiteral("The Smiths")
        };
        for (const QString &artist : undelimitedArtistPrefixes) {
            const QString prefix = artist + QLatin1Char(' ');
            if (!title.startsWith(prefix, Qt::CaseInsensitive))
                continue;
            title = artist + QStringLiteral(" - ")
                    + title.mid(prefix.size()).trimmed();
            break;
        }
    }
    static const QRegularExpression leadingEdition(
        QStringLiteral(
            "^\\s*([^-\\r\\n]{1,40}?)\\s+Version\\s+"
            "(.+?)\\s+-\\s+(.+?)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch editionMatch = leadingEdition.match(title);
    if (editionMatch.hasMatch()) {
        const QString edition = titleCasedIfAllLowercase(
            editionMatch.captured(1).trimmed());
        const QString artist = titleCasedIfAllCaps(
            editionMatch.captured(2).trimmed());
        const QString song = titleCasedIfAllCaps(
            editionMatch.captured(3).trimmed());
        title = artist + QStringLiteral(" - ") + song
                + QStringLiteral(" (") + edition + QLatin1Char(')');
    }
    static const QRegularExpression brmcArtist(
        QStringLiteral("^Brmc(?=\\s+-\\s+)"));
    title.replace(brmcArtist, QStringLiteral("BRMC"));
    title = title.trimmed();
    static const QHash<QString, QString> malformedTitleAliases{
        {QStringLiteral("trentemoller feat sune rose wagner deceive"),
         QStringLiteral("Trentemoller Ft. Sune Rose Wagner - Deceive")}
    };
    return malformedTitleAliases.value(titleLookupKey(title), title);
}

QString normalizedPantsTitle(QString title)
{
    // A small number of Pants titles omit the original artist entirely.
    // Keep those verified attributions in one map shared by each matching
    // grammar instead of scattering video-specific replacements.
    static const QHash<QString, QString> canonicalArtistBySong{
        {QStringLiteral("eye of the tiger"), QStringLiteral("Survivor")},
        {QStringLiteral("i wonder what's inside your butthole"),
         QStringLiteral("Jo Lee")}
    };

    static const QRegularExpression creditedPerformanceTitle(
        QStringLiteral(
            "^\\s*[\\\"“”]([^\\\"“”\\r\\n]{1,200})[\\\"“”]"
            "\\s+(?:Written\\s+and\\s+)?Performed\\s+by\\s+(.+?)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch performanceMatch =
        creditedPerformanceTitle.match(title);
    if (performanceMatch.hasMatch()) {
        return performanceMatch.captured(2).trimmed()
               + QStringLiteral(" - ")
               + performanceMatch.captured(1).trimmed();
    }

    static const QRegularExpression creditedParodyTitle(
        QStringLiteral(
            "^\\s*[\\\"“”]([^\\\"“”\\r\\n]{1,200})[\\\"“”]"
            "\\s+by\\s+(.+?)(?:\\s+\\(([^()]*)\\))?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch parodyMatch = creditedParodyTitle.match(title);
    if (parodyMatch.hasMatch()) {
        QString normalized = parodyMatch.captured(2).trimmed()
                             + QStringLiteral(" - ")
                             + parodyMatch.captured(1).trimmed();
        QString qualifier = parodyMatch.captured(3).trimmed();
        if (!qualifier.isEmpty()) {
            static const QRegularExpression parodyWord(
                QStringLiteral("\\bParody\\b"),
                QRegularExpression::CaseInsensitiveOption);
            qualifier.replace(parodyWord, QStringLiteral("Parody"));
            normalized += QStringLiteral(" (") + qualifier + QLatin1Char(')');
        }
        return normalized;
    }

    static const QRegularExpression liveCoverTitle(
        QStringLiteral(
            "^\\s*[\\\"“”]([^\\\"“”\\r\\n]{1,200})[\\\"“”]"
            "\\s+\\([\\\"“”]([^\\\"“”\\r\\n]{1,200})[\\\"“”]\\s+Cover\\)"
            "\\s+at\\s+.+?\\s+\\(([^()]*)\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch coverMatch = liveCoverTitle.match(title);
    if (coverMatch.hasMatch()) {
        return coverMatch.captured(3).trimmed()
               + QStringLiteral(" - ")
               + coverMatch.captured(1).trimmed()
               + QStringLiteral(" (")
               + coverMatch.captured(2).trimmed()
               + QStringLiteral(" Cover)");
    }

    static const QRegularExpression animalSoundsTitle(
        QStringLiteral(
            "^\\s*[\\\"“”]([^\\\"“”\\r\\n]{1,200})[\\\"“”]"
            "\\s+Karaoke\\s+with\\s+Animal\\s+Sounds"
            "\\s+instead\\s+of\\s+Instruments\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch animalMatch = animalSoundsTitle.match(title);
    if (animalMatch.hasMatch()) {
        const QString song = animalMatch.captured(1).trimmed();
        const QString artist = canonicalArtistBySong.value(song.toCaseFolded());
        return (artist.isEmpty() ? QString{} : artist + QStringLiteral(" - "))
               + song + QStringLiteral(" (Animal Sounds)");
    }

    static const QRegularExpression karaokeCoverVersionTitle(
        QStringLiteral(
            "^\\s*(.+?)\\s*[\\[\\(]\\s*Karaoke\\s+Version\\s+of\\s+"
            "[\\\"“”][^\\\"“”\\r\\n]{1,200}[\\\"“”]\\s+Version\\s*"
            "[\\]\\)]\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch karaokeCoverMatch =
        karaokeCoverVersionTitle.match(title);
    if (karaokeCoverMatch.hasMatch()) {
        const QString song = karaokeCoverMatch.captured(1).trimmed();
        const QString artist = canonicalArtistBySong.value(song.toCaseFolded());
        if (!artist.isEmpty())
            return artist + QStringLiteral(" - ") + song;
    }

    static const QRegularExpression liveTourKaraokeVersion(
        QStringLiteral(
            "^\\s*(.+?)\\s*[\\[\\(]\\s*Karaoke\\s+Version\\s+of\\s+the\\s+"
            "Live\\s+Version\\s+from\\s+the\\s+['\\\"“”]"
            "([^'\\\"“”\\r\\n]{1,120})['\\\"“”]\\s+Tour\\s*[\\]\\)]\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch liveTourMatch =
        liveTourKaraokeVersion.match(title);
    if (liveTourMatch.hasMatch()) {
        return liveTourMatch.captured(1).trimmed()
               + QStringLiteral(" (")
               + liveTourMatch.captured(2).trimmed()
               + QStringLiteral(" Tour Live)");
    }

    static const QRegularExpression pantsKaraokeVersionQualifier(
        QStringLiteral(
            "^\\s*(.+?)\\s*\\(\\s*Pants\\s+Karaoke\\s+Version\\s+of\\s+"
            "([^()\\r\\n]{1,200}?)\\s*\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch pantsVersionMatch =
        pantsKaraokeVersionQualifier.match(title);
    if (pantsVersionMatch.hasMatch()) {
        return pantsVersionMatch.captured(1).trimmed()
               + QStringLiteral(" (")
               + pantsVersionMatch.captured(2).trimmed()
               + QLatin1Char(')');
    }

    static const QRegularExpression barnyardAnimalRemix(
        QStringLiteral("\\(\\s*Barnyard\\s+Animal\\s+Remix\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    title.replace(barnyardAnimalRemix, QStringLiteral("(Animal Sounds)"));
    return title;
}

QString normalizedKaraokeOfficeTitle(QString title)
{
    static const QRegularExpression trailingKaraokeVersion(
        QStringLiteral(
            "\\s*(?:\\(\\s*)?Karaoke\\s+Version(?:\\s*\\))?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    title.remove(trailingKaraokeVersion);
    title = title.trimmed();

    const QString key = titleLookupKey(title);
    static const QHash<QString, QString> malformedTitleAliases{
        {QStringLiteral("wiz khalifa see you again ft charlie puth"),
         QStringLiteral("Wiz Khalifa ft Charlie Puth - See You Again")},
        {QStringLiteral("i bet on losing dogs mitski"),
         QStringLiteral("Mitski - I Bet on Losing Dogs")},
        {QStringLiteral("28 zach bryan"),
         QStringLiteral("Zach Bryan - 28")}
    };
    return malformedTitleAliases.value(key, title);
}

QString withCompactEditionLabels(QString title)
{
    static const QRegularExpression yearVersionWithQualifier(
        QStringLiteral(
            "\\(\\s*([0-9]{4})\\s+Version\\s*;\\s*"
            "([^()\\r\\n]{1,120}?)\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype offset = 0;
    while (true) {
        const QRegularExpressionMatch match =
            yearVersionWithQualifier.match(title, offset);
        if (!match.hasMatch())
            break;
        const QString replacement = QStringLiteral("(")
                                    + match.captured(2).trimmed()
                                    + QStringLiteral(") (")
                                    + match.captured(1) + QLatin1Char(')');
        title.replace(match.capturedStart(), match.capturedLength(), replacement);
        offset = match.capturedStart() + replacement.size();
    }

    static const QRegularExpression leadingVersionEdition(
        QStringLiteral(
            "\\(\\s*Version\\s+([^()\\r\\n]{1,120}?)\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    offset = 0;
    while (true) {
        const QRegularExpressionMatch match =
            leadingVersionEdition.match(title, offset);
        if (!match.hasMatch())
            break;
        const QString replacement = QStringLiteral("(")
                                    + titleCasedIfAllLowercase(
                                          match.captured(1).trimmed())
                                    + QLatin1Char(')');
        title.replace(match.capturedStart(), match.capturedLength(), replacement);
        offset = match.capturedStart() + replacement.size();
    }

    static const QString trailingEditionNames = QStringLiteral(
        "Acoustic|Album|Live|Official|Original|Single|Video");
    static const QRegularExpression parenthesizedEdition(
        QStringLiteral(
            "\\(\\s*([^()\\r\\n]{1,120}?)\\s+Version\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    offset = 0;
    while (true) {
        const QRegularExpressionMatch match = parenthesizedEdition.match(title, offset);
        if (!match.hasMatch())
            break;
        const QString replacement = QStringLiteral("(")
                                    + titleCasedIfAllLowercase(
                                          match.captured(1).trimmed())
                                    + QLatin1Char(')');
        title.replace(match.capturedStart(), match.capturedLength(), replacement);
        offset = match.capturedStart() + replacement.size();
    }

    static const QRegularExpression trailingEdition(
        QStringLiteral("\\s+(%1)\\s+Version\\s*$")
            .arg(trailingEditionNames),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch trailingMatch = trailingEdition.match(title);
    if (trailingMatch.hasMatch()) {
        title = title.left(trailingMatch.capturedStart()).trimmed()
                + QStringLiteral(" (")
                + titleCasedIfAllLowercase(
                      trailingMatch.captured(1).trimmed())
                + QLatin1Char(')');
    }
    return title;
}

QString normalizedTitleSeparators(QString title)
{
    static const QRegularExpression bulletSeparator(QStringLiteral("\\s*•\\s*"));
    static const QRegularExpression asciiDashSeparator(
        QStringLiteral("(?:\\s+-\\s*|\\s*-\\s+)"));
    static const QRegularExpression enDashSeparator(QStringLiteral("\\s*–\\s*"));
    static const QRegularExpression emDashSeparator(QStringLiteral("\\s*—\\s*"));
    static const QRegularExpression repeatedHorizontalSpace(
        QStringLiteral("[\\t ]{2,}"));
    static const QRegularExpression leadingContext(
        QStringLiteral(
            "^\\s*\\([^()\\r\\n]{1,80}\\)\\s+"
            "(?=\\S(?:.*\\S)?\\s+(?:-|–|—)\\s+)"));
    static const QRegularExpression quotedWeirdAl(
        QStringLiteral("^[\\\"“”]Weird\\s+Al[\\\"“”]\\s+Yankovic\\b"),
        QRegularExpression::CaseInsensitiveOption);

    title.remove(leadingContext);
    title.replace(bulletSeparator, QStringLiteral(" - "));
    title.replace(asciiDashSeparator, QStringLiteral(" - "));
    title.replace(enDashSeparator, QStringLiteral(" – "));
    title.replace(emDashSeparator, QStringLiteral(" - "));
    title.replace(repeatedHorizontalSpace, QStringLiteral(" "));
    title.replace(quotedWeirdAl, QStringLiteral("Weird Al Yankovic"));
    title.replace(QLatin1Char('['), QLatin1Char('('));
    title.replace(QLatin1Char(']'), QLatin1Char(')'));

    // Featured credits are artist metadata. Canonicalize credits already on
    // the artist side and move misplaced title-side credits there as well.
    qsizetype artistSeparator = title.indexOf(QStringLiteral(" - "));
    const qsizetype enDashArtistSeparator = title.indexOf(QStringLiteral(" – "));
    if (artistSeparator < 0 ||
        (enDashArtistSeparator >= 0 && enDashArtistSeparator < artistSeparator)) {
        artistSeparator = enDashArtistSeparator;
    }
    if (artistSeparator > 0) {
        QString artist = title.left(artistSeparator);
        QString song = title.mid(artistSeparator + 3).trimmed();
        const QString separator = title.mid(artistSeparator, 3);
        static const QRegularExpression parenthesizedFeaturedCredit(
            QStringLiteral(
                "\\(\\s*(?:Featuring|Feat|Ft)\\.?\\s+([^()]*)\\s*\\)"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression featuredCredit(
            QStringLiteral("\\b(?:Featuring|Feat|Ft)\\.?(?=\\s)"),
            QRegularExpression::CaseInsensitiveOption);
        artist.replace(parenthesizedFeaturedCredit,
                       QStringLiteral(" Ft. \\1"));
        artist.replace(featuredCredit, QStringLiteral("Ft."));
        artist = artist.simplified();

        QString guest;
        static const QRegularExpression parenthesizedTitleCredit(
            QStringLiteral(
                "^(.*?)\\s*\\(\\s*"
                "(?:(.*?)\\s*(?:-|–|—|;|,)\\s*)?"
                "(?:Featuring|Feat|Ft)\\.?\\s+"
                "([^()]*)\\s*\\)(.*)$"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression trailingTitleCredit(
            QStringLiteral(
                "^(.+?)\\s+(?:Featuring|Feat|Ft)\\.?\\s+([^()]+?)"
                "(\\s+(?:\\([^()]*\\)\\s*)*)?$"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression leadingTitleCredit(
            QStringLiteral(
                "^\\s*(?:Featuring|Feat|Ft)\\.?\\s+(.+?)"
                "\\s+(?:-|–|—)\\s+(.+?)\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch parenthesizedMatch =
            parenthesizedTitleCredit.match(song);
        if (parenthesizedMatch.hasMatch()) {
            guest = parenthesizedMatch.captured(3).trimmed();
            song = parenthesizedMatch.captured(1).trimmed();
            const QString qualifier = parenthesizedMatch.captured(2).trimmed();
            if (!qualifier.isEmpty())
                song += QStringLiteral(" (") + qualifier + QLatin1Char(')');
            const QString suffix = parenthesizedMatch.captured(4).trimmed();
            if (!suffix.isEmpty())
                song += QLatin1Char(' ') + suffix;
        } else {
            const QRegularExpressionMatch leadingMatch =
                leadingTitleCredit.match(song);
            if (leadingMatch.hasMatch()) {
                guest = leadingMatch.captured(1).trimmed();
                song = leadingMatch.captured(2).trimmed();
            } else {
                const QRegularExpressionMatch trailingMatch =
                    trailingTitleCredit.match(song);
                if (trailingMatch.hasMatch()) {
                    guest = trailingMatch.captured(2).trimmed();
                    song = (trailingMatch.captured(1).trimmed()
                            + QLatin1Char(' ')
                            + trailingMatch.captured(3).trimmed()).trimmed();
                }
            }
        }
        if (!guest.isEmpty()) {
            static const QRegularExpression canonicalFeaturedCredit(
                QStringLiteral("\\bFt\\.\\s"),
                QRegularExpression::CaseInsensitiveOption);
            artist += canonicalFeaturedCredit.match(artist).hasMatch()
                ? QStringLiteral(" & ") + guest
                : QStringLiteral(" Ft. ") + guest;
        }
        title = artist + separator + song;
    }

    title = withCompactEditionLabels(title);
    static const QRegularExpression belaLugosiPossessive(
        QStringLiteral("\\bBela\\s+Lugosis\\s+Dead\\b"),
        QRegularExpression::CaseInsensitiveOption);
    title.replace(belaLugosiPossessive, QStringLiteral("Bela Lugosi's Dead"));
    return title.trimmed();
}

QString normalizedJLoInstruTitle(const QString &title)
{
    QString normalized = normalizedTitleSeparators(title);
    static const QRegularExpression brandedSongFirstTitle(
        QStringLiteral(
            "^(.+?)\\s+-\\s+(.+?)\\s+-\\s+"
            "(?:Instrumental(?:\\s+Version)?\\s+-\\s+"
            "Karaoke(?:\\s+Lyrics)?|"
            "Karaoke\\s+-\\s+Instrumental(?:\\s+Version)?)"
            "(\\s*\\([^()]+\\))?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = brandedSongFirstTitle.match(normalized);
    if (match.hasMatch()) {
        normalized = match.captured(2).trimmed() + QStringLiteral(" - ")
                     + match.captured(1).trimmed();
        const QString qualifier = match.captured(3).trimmed();
        if (!qualifier.isEmpty())
            normalized += QStringLiteral(" ") + qualifier;
    }

    // This channel is normally song-first, but a small number of uploads are
    // already artist-first. Keep verified exceptions in one canonical map so
    // the dominant grammar remains deterministic.
    static const QHash<QString, QString> artistFirstAliases{
        {QStringLiteral("1979 - the smashing pumpkins"),
         QStringLiteral("The Smashing Pumpkins - 1979")}
    };
    return artistFirstAliases.value(normalized.toCaseFolded(), normalized);
}

QString withoutOffbeatKaraokeQualifier(const QString &title)
{
    QString cleaned = title;
    static const QRegularExpression trailingQualifier(
        QStringLiteral("\\s*\\(([^()]*)\\)\\s*$"));
    const QRegularExpressionMatch match = trailingQualifier.match(title);
    static const QRegularExpression karaokeVersionSegment(
        QStringLiteral(
            "^Karaoke\\s+Version(?:\\s*:\\s*Key\\s+"
            "[A-G](?:(?:\\s*(?:Sharp|Flat))|[#♯b♭])?"
            "(?:m|\\s+(?:Major|Minor))?)?$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QString keyValuePattern = QStringLiteral(
        "[A-G](?:(?:\\s*(?:Sharp|Flat))|[#♯b♭])?"
        "(?:m|\\s+(?:Major|Minor))?");
    static const QRegularExpression musicalKey(
        QStringLiteral("^(?:Key\\s+(?:of\\s+)?)?%1$")
            .arg(keyValuePattern),
        QRegularExpression::CaseInsensitiveOption);
    if (match.hasMatch()) {
        const QStringList qualifiers = match.captured(1).split(
            QLatin1Char(';'), Qt::SkipEmptyParts);
        bool foundKaraokeVersion = false;
        QStringList retained;
        for (const QString &qualifier : qualifiers) {
            const QString trimmed = qualifier.trimmed();
            if (karaokeVersionSegment.match(trimmed).hasMatch()) {
                foundKaraokeVersion = true;
            } else if (!musicalKey.match(trimmed).hasMatch()) {
                retained.append(trimmed);
            }
        }
        if (foundKaraokeVersion) {
            cleaned = title.left(match.capturedStart()).trimmed();
            if (!retained.isEmpty()) {
                cleaned += QStringLiteral(" (")
                           + retained.join(QStringLiteral("; "))
                           + QLatin1Char(')');
            }
        }
    }
    static const QRegularExpression trailingSemicolonKey(
        QStringLiteral(
            "\\s*;\\s*Key\\s+(?:of\\s+)?[A-G]"
            "(?:(?:\\s*(?:Sharp|Flat))|[#♯b♭])?"
            "(?:m|\\s+(?:Major|Minor))?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailingParenthesizedKey(
        QStringLiteral(
            "\\s*\\(\\s*Key\\s+(?:of\\s+)?[A-G]"
            "(?:(?:\\s*(?:Sharp|Flat))|[#♯b♭])?"
            "(?:m|\\s+(?:Major|Minor))?\\s*\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    cleaned.remove(trailingSemicolonKey);
    cleaned.remove(trailingParenthesizedKey);
    return cleaned;
}

QString withoutJustSingKaraokeBranding(QString title)
{
    static const QRegularExpression karaokeMarker(
        QStringLiteral(
            "\\s*(?:-\\s*)?\\(\\s*"
            "(?:(?:High\\s+Quality\\s+)?Karaoke(?:\\s+Version)?|"
            "Karaoke\\s+(?:Alta\\s+Qualidade|Com\\s+Letra))"
            "\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    title.remove(karaokeMarker);
    return title.trimmed();
}

QString karaFunCatalogTitle(const QString &rawTitle)
{
    // Current KaraFun uploads are song-first and pipe-delimited. Retain the
    // alternate artist-first parenthesized form and the verified legacy
    // song-first form in the same positive grammar so filtering and display
    // cleanup cannot diverge.
    static const QRegularExpression songFirstTitle(
        QStringLiteral(
            "^\\s*(.+)\\s+-\\s+([^|\\r\\n]+?)\\s*"
            "\\|\\s*Karaoke\\s+Version\\s*\\|\\s*KaraFun\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch songFirstMatch = songFirstTitle.match(rawTitle);
    if (songFirstMatch.hasMatch()) {
        return songFirstMatch.captured(2).trimmed() + QStringLiteral(" - ")
               + songFirstMatch.captured(1).trimmed();
    }

    static const QRegularExpression legacySongFirstTitle(
        QStringLiteral(
            "^\\s*Karaoke\\s+(.+)\\s+-\\s+(.+?)\\s*\\*\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch legacySongFirstMatch =
        legacySongFirstTitle.match(rawTitle);
    if (legacySongFirstMatch.hasMatch()) {
        return legacySongFirstMatch.captured(2).trimmed()
               + QStringLiteral(" - ")
               + legacySongFirstMatch.captured(1).trimmed();
    }

    static const QRegularExpression artistFirstTitle(
        QStringLiteral(
            "^\\s*(.+?)\\s+-\\s+(.+?)\\s*"
            "\\(\\s*Karaoke\\s+Version\\s*\\)"
            "\\s*(?:\\|\\s*KaraFun\\s*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch artistFirstMatch = artistFirstTitle.match(rawTitle);
    if (!artistFirstMatch.hasMatch())
        return {};
    return artistFirstMatch.captured(1).trimmed() + QStringLiteral(" - ")
           + artistFirstMatch.captured(2).trimmed();
}

QString sourceIdForChannelId(const QString &channelId)
{
    for (const CatalogSource &source : kCatalogSources) {
        if (channelId == source.channelId)
            return source.id;
    }
    return {};
}

int sourcePriority(const QString &sourceId)
{
    for (int index = 0; index < kCatalogSources.size(); ++index) {
        if (sourceId == kCatalogSources.at(index).id)
            return index;
    }
    return kCatalogSources.size();
}

QString sourceIdForObject(const QJsonObject &object, const QString &rawTitle)
{
    const QStringList identityFields{
        QStringLiteral("channel_id"),
        QStringLiteral("playlist_channel_id"),
        QStringLiteral("playlist_id")
    };
    for (const QString &field : identityFields) {
        const QString value = object.value(field).toString().trimmed();
        if (value.isEmpty())
            continue;
        return sourceIdForChannelId(value);
    }

    // Flat playlist records normally contain playlist_channel_id. Retain a
    // narrow fallback for older yt-dlp output that omitted channel identity.
    static const QRegularExpression obsKureBrand(
        QStringLiteral("(?:^|\\s)ObsKure\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (obsKureBrand.match(rawTitle).hasMatch())
        return kObsKureSourceId;
    static const QRegularExpression genericKaraokeBrand(
        QStringLiteral("\\(Karaoke\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    if (genericKaraokeBrand.match(rawTitle).hasMatch())
        return {};
    return kFunboxSourceId;
}

bool isKnownSourceId(const QString &sourceId)
{
    for (const CatalogSource &source : kCatalogSources) {
        if (sourceId == source.id)
            return true;
    }
    return false;
}

bool isExcludedCatalogTitle(const QString &sourceId, const QString &rawTitle)
{
    if (sourceId == kKaraFunSourceId)
        return karaFunCatalogTitle(rawTitle).isEmpty();
    if (sourceId != kPantsKaraokeSourceId)
        return false;

    // This upload is a creator-specific parody without an attributable artist,
    // so it cannot be represented by the catalog's Artist - Song contract.
    static const QRegularExpression unattributedPantsParody(
        QStringLiteral(
            "^\\s*25\\s+Minutes\\s+or\\s+Less\\s*"
            "\\(\\s*Karaoke\\s+Parody\\s+of\\s+"
            "[\\\"“”]25\\s+Minutes\\s+to\\s+Go[\\\"“”]"),
        QRegularExpression::CaseInsensitiveOption);
    return unattributedPantsParody.match(rawTitle).hasMatch();
}

QHash<QString, int> sourceCounts(const QVariantList &items)
{
    QHash<QString, int> counts;
    for (const QVariant &item : items) {
        const QString sourceId = item.toMap().value(QStringLiteral("sourceId")).toString();
        if (isKnownSourceId(sourceId))
            ++counts[sourceId];
    }
    return counts;
}

}

KaraokeBackend::KaraokeBackend(const QString &appRoot, const QString &dataRoot,
                               QObject *parent)
    : QObject(parent)
    , m_appRoot(appRoot)
    , m_dataRoot(dataRoot)
{
    m_catalogJob = new YouTubeJob(m_appRoot, this);
    connect(m_catalogJob, &YouTubeJob::outputReady, this,
            [this](const QByteArray &output) {
        m_catalogOutputBuffer += output;
        consumeCatalogOutput();
    });
    connect(m_catalogJob, &YouTubeJob::completed, this,
            &KaraokeBackend::finishCatalogRefresh);

    m_prefetchJob = new YouTubeJob(m_appRoot, this);
    connect(m_prefetchJob, &YouTubeJob::outputReady, this,
            [](const QByteArray &) {});
    connect(m_prefetchJob, &YouTubeJob::completed, this,
            &KaraokeBackend::finishPrefetch);
    loadQueue();
}

KaraokeBackend::~KaraokeBackend()
{
    m_catalogJob->cancelSilently();
    stopPrefetchProcess(true);
}

QString KaraokeBackend::cleanedTitle(const QString &rawTitle,
                                     const QString &sourceId)
{
    if (sourceId == kKaraFunSourceId) {
        const QString catalogTitle = karaFunCatalogTitle(rawTitle);
        if (!catalogTitle.isEmpty())
            return normalizedTitleSeparators(catalogTitle);
    }

    static const QRegularExpression funboxSuffix(
        QStringLiteral("\\s*\\(Funbox Karaoke,\\s*([0-9]{4}(?:/[0-9]{4})*)\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch funboxMatch = funboxSuffix.match(rawTitle);
    if (funboxMatch.hasMatch()) {
        return normalizedTitleSeparators(
            rawTitle.left(funboxMatch.capturedStart()).trimmed()
            + QStringLiteral(" (") + funboxMatch.captured(1) + QLatin1Char(')'));
    }

    static const QRegularExpression obsKureSuffix(
        QStringLiteral(
            "\\s*-\\s*(?:(?:Best|Duet|Dueto|Solo)\\s+)*Karaoke"
            "(?:\\s+Version\\s*-\\s*)?\\s*Instrumental"
            "(?:\\s+(?:Lyrics|Letra))?(?:\\s*-?\\s*Best\\s+Version)?"
            "(?:\\s*-\\s*Melhor\\s+Vers(?:a|ã)o)?(?:\\s*-?\\s*ObsKure)?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression obsKureInstrumentalLyricsSuffix(
        QStringLiteral(
            "\\s*-\\s*Instrumental\\s+(?:Lyrics|Letra)"
            "\\s*-?\\s*ObsKure\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    QString cleaned = rawTitle.trimmed();
    const bool hadObsKureSuffix = obsKureSuffix.match(cleaned).hasMatch()
                                  || obsKureInstrumentalLyricsSuffix.match(cleaned).hasMatch();
    cleaned.remove(obsKureSuffix);
    cleaned.remove(obsKureInstrumentalLyricsSuffix);
    cleaned = cleaned.trimmed();

    if (sourceId == kPantsKaraokeSourceId)
        cleaned = normalizedPantsTitle(cleaned);
    if (sourceId == kKaraokeOfficeSourceId)
        cleaned = normalizedKaraokeOfficeTitle(cleaned);
    if (sourceId == kJustSingKaraokeSourceId)
        cleaned = withoutJustSingKaraokeBranding(cleaned);

    // Some ObsKure generations place quality/mix labels immediately before
    // the Karaoke suffix. Strip only that anchored trailing descriptor chain
    // so title annotations such as live venues and soundtrack names survive.
    static const QRegularExpression obsKureTrailingDescriptor(
        QStringLiteral(
            "\\s*(?:-\\s*(?:Best\\s+Version|Original\\s+Sound|Som\\s+Original|"
            "Duet|Dueto|Solo)|\\(Best\\s+Version\\))\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    while (hadObsKureSuffix &&
           obsKureTrailingDescriptor.match(cleaned).hasMatch()) {
        cleaned.remove(obsKureTrailingDescriptor);
        cleaned = cleaned.trimmed();
    }

    if (sourceId == kJLoInstruSourceId)
        cleaned = normalizedJLoInstruTitle(cleaned);

    bool hadOneMusicBranding = false;
    if (sourceId == kOneMusicSourceId)
        cleaned = withoutOneMusicBranding(cleaned, &hadOneMusicBranding);

    static const QRegularExpression dashKaraokeMarker(
        QStringLiteral("\\s*(?:-|•)\\s*Karaoke(?=\\s*(?:-|\\(|$))"),
        QRegularExpression::CaseInsensitiveOption);
    cleaned.remove(dashKaraokeMarker);

    static const QRegularExpression leadingOneMusicMarker(
        QStringLiteral("^\\s*Karaoke\\s+"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailingOneMusicMarker(
        QStringLiteral("Karaoke\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const bool hadOneMusicMarker = hadOneMusicBranding ||
                                   leadingOneMusicMarker.match(cleaned).hasMatch() ||
                                   trailingOneMusicMarker.match(cleaned).hasMatch();
    cleaned.remove(leadingOneMusicMarker);
    cleaned.remove(trailingOneMusicMarker);
    cleaned = cleaned.trimmed();
    if (hadOneMusicMarker)
        cleaned = titleCasedIfAllCaps(cleaned);
    if (sourceId == kOneMusicSourceId)
        cleaned = normalizedOneMusicStructure(cleaned);

    static const QRegularExpression ccKaraokeSuffix(
        QStringLiteral(
            "\\s*\\(CC\\s+Karaoke\\s*/\\s*Instrumental\\)"
            "\\s*(?:\\[UVR\\])?\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const bool hadCCKaraokeSuffix = ccKaraokeSuffix.match(cleaned).hasMatch();
    cleaned.remove(ccKaraokeSuffix);
    if (hadCCKaraokeSuffix) {
        static const QRegularExpression trailingUvr(
            QStringLiteral("\\s*\\[UVR\\]\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        cleaned.remove(trailingUvr);
    }

    static const QRegularExpression legacyCCKaraokeSuffix(
        QStringLiteral(
            "\\s*(?:(?:\\[CC\\]|\\(CC\\))\\s*)?"
            "(?:\\[Karaoke\\s+(?:Instrumental(?:\\s+Version)?|Version)\\]|"
            "\\(Karaoke\\s+(?:Instrumental(?:\\s+Version)?|Version)\\))"
            "\\s*(?:(?:\\[UVR\\]|\\(UVR\\))\\s*)?$"),
        QRegularExpression::CaseInsensitiveOption);
    cleaned.remove(legacyCCKaraokeSuffix);
    static const QRegularExpression decoratedCCKaraokeSuffix(
        QStringLiteral(
            "\\s*\\(CC\\)\\s*(?:🎤\\s*)?"
            "\\[Karaoke\\]\\s*\\[Instrumental\\]\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    cleaned.remove(decoratedCCKaraokeSuffix);
    cleaned = cleaned.trimmed();

    static const QRegularExpression karaokeVersionMarker(
        QStringLiteral(
            "\\s*(?:\\(\\s*Karaoke\\s+Version\\s*0?\\s*\\)?|"
            "\\[\\s*Karaoke\\s+Version\\s*0?\\s*\\]?)(?=\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    cleaned.remove(karaokeVersionMarker);
    cleaned = cleaned.trimmed();

    static const QRegularExpression karaokeYearSuffix(
        QStringLiteral("\\s*\\(Karaoke\\)\\s*([0-9]{4})\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch yearMatch = karaokeYearSuffix.match(cleaned);
    if (yearMatch.hasMatch()) {
        return normalizedTitleSeparators(
            cleaned.left(yearMatch.capturedStart()).trimmed()
            + QStringLiteral(" (") + yearMatch.captured(1) + QLatin1Char(')'));
    }

    static const QRegularExpression karaokeVersionSuffix(
        QStringLiteral("\\s*\\((?:(Solo|Duet)\\s+Karaoke|Karaoke\\s+(Solo|Duet))\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch versionMatch = karaokeVersionSuffix.match(cleaned);
    if (versionMatch.hasMatch()) {
        const QString version = versionMatch.captured(1).isEmpty()
            ? versionMatch.captured(2) : versionMatch.captured(1);
        return normalizedTitleSeparators(
            cleaned.left(versionMatch.capturedStart()).trimmed()
            + QStringLiteral(" (") + version + QLatin1Char(')'));
    }

    if (sourceId == kLemmyCautionSourceId) {
        static const QRegularExpression repeatedArtistLiveSuffix(
            QStringLiteral(
                "^\\s*(.+?)\\s+-\\s+(.+?)\\s*\\(Karaoke\\)\\s*"
                "([0-9]{4})\\s+(?:The\\s+)?\\1\\s+Live\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch repeatedArtistLiveMatch =
            repeatedArtistLiveSuffix.match(cleaned);
        if (repeatedArtistLiveMatch.hasMatch()) {
            cleaned = repeatedArtistLiveMatch.captured(1).trimmed()
                      + QStringLiteral(" - ")
                      + repeatedArtistLiveMatch.captured(2).trimmed()
                      + QStringLiteral(" (Live) (")
                      + repeatedArtistLiveMatch.captured(3)
                      + QLatin1Char(')');
        }

        static const QRegularExpression preKaraokeVersionSuffix(
            QStringLiteral(
                "\\s+([\\p{L}\\p{N}]+)\\s+Version"
                "\\s*\\(Karaoke\\)\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch preKaraokeVersionMatch =
            preKaraokeVersionSuffix.match(cleaned);
        if (preKaraokeVersionMatch.hasMatch()) {
            const QString qualifier = titleCasedIfAllLowercase(
                preKaraokeVersionMatch.captured(1).trimmed());
            cleaned = cleaned.left(preKaraokeVersionMatch.capturedStart()).trimmed()
                      + QStringLiteral(" (") + qualifier + QLatin1Char(')');
        }

        static const QRegularExpression namedVersionSuffix(
            QStringLiteral(
                "\\s*\\(Karaoke\\)\\s*([^()\\r\\n]{1,80}?)"
                "\\s+Version\\s*$"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch namedVersionMatch =
            namedVersionSuffix.match(cleaned);
        if (namedVersionMatch.hasMatch()) {
            const QString qualifier = titleCasedIfAllLowercase(
                namedVersionMatch.captured(1).trimmed());
            cleaned = cleaned.left(namedVersionMatch.capturedStart()).trimmed()
                      + QStringLiteral(" (") + qualifier + QLatin1Char(')');
        }
    }

    static const QRegularExpression karaokeMarker(
        QStringLiteral(
            "\\s*(?:\\(\\s*Karaoke\\s*\\)|"
            "\\[\\s*Karaoke\\s*[\\]\\}])(?=\\s|$)"),
        QRegularExpression::CaseInsensitiveOption);
    cleaned.remove(karaokeMarker);

    static const QRegularExpression parenthesizedSevenInchVersion(
        QStringLiteral(
            "\\(\\s*7\\s*(?:-|\\s)?Inch(?:es)?"
            "(?:\\s+(?:Version|Mix))?\\s*\\)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression trailingSevenInchVersion(
        QStringLiteral(
            "\\s+7\\s*(?:-|\\s)?Inch(?:es)?\\s+(?:Version|Mix)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    cleaned.replace(parenthesizedSevenInchVersion, QStringLiteral("(7\")"));
    cleaned.replace(trailingSevenInchVersion, QStringLiteral(" (7\")"));
    if (sourceId == kOffbeatKaraokeSourceId)
        cleaned = withoutOffbeatKaraokeQualifier(cleaned);
    return normalizedTitleSeparators(cleaned);
}

QString KaraokeBackend::deduplicationKey(const QString &title)
{
    QString comparable = cleanedTitle(title);
    static const QRegularExpression funboxYear(
        QStringLiteral("\\s*\\([0-9]{4}(?:/[0-9]{4})*\\)\\s*$"));
    comparable.remove(funboxYear);
    comparable.replace(QLatin1Char('&'), QStringLiteral(" and "));
    comparable = comparable.normalized(QString::NormalizationForm_D)
                     .toCaseFolded();

    static const QRegularExpression combiningMarks(QStringLiteral("[\\p{M}]+"));
    static const QRegularExpression apostrophes(
        QStringLiteral("[\\x{0027}\\x{2018}\\x{2019}\\x{02BC}]"));
    static const QRegularExpression punctuation(QStringLiteral("[^\\p{L}\\p{N}]+"));
    comparable.remove(combiningMarks);
    comparable.remove(apostrophes);
    comparable.replace(punctuation, QStringLiteral(" "));

    const QSet<QString> ignoredArticles{
        QStringLiteral("a"), QStringLiteral("an"), QStringLiteral("and"),
        QStringLiteral("the")
    };
    QStringList tokens;
    const QStringList candidates = comparable.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &candidate : candidates) {
        if (!ignoredArticles.contains(candidate))
            tokens.append(candidate);
    }
    return tokens.join(QLatin1Char(' '));
}

QVariantList KaraokeBackend::deduplicatedCatalog(const QVariantList &items)
{
    QHash<QString, int> preferredPriorityByKey;
    for (const QVariant &item : items) {
        const QVariantMap song = item.toMap();
        const QString sourceId = song.value(QStringLiteral("sourceId")).toString();
        if (!isKnownSourceId(sourceId))
            continue;
        const QString key = deduplicationKey(
            song.value(QStringLiteral("displayTitle"),
                       song.value(QStringLiteral("rawTitle"))).toString());
        if (key.isEmpty())
            continue;
        const int priority = sourcePriority(sourceId);
        const auto existing = preferredPriorityByKey.constFind(key);
        if (existing == preferredPriorityByKey.cend() || priority < existing.value())
            preferredPriorityByKey[key] = priority;
    }

    QVariantList result;
    QSet<QString> videoIds;
    QSet<QString> titleKeys;
    result.reserve(items.size());
    for (const QVariant &item : items) {
        const QVariantMap song = item.toMap();
        const QString videoId = song.value(QStringLiteral("videoId")).toString();
        if (!isValidVideoId(videoId) || videoIds.contains(videoId))
            continue;
        const QString sourceId = song.value(QStringLiteral("sourceId")).toString();
        if (!isKnownSourceId(sourceId))
            continue;
        const QString key = deduplicationKey(
            song.value(QStringLiteral("displayTitle"),
                       song.value(QStringLiteral("rawTitle"))).toString());
        if (!key.isEmpty() &&
            sourcePriority(sourceId) > preferredPriorityByKey.value(key)) {
            continue;
        }
        // Keep the first upload from the winning source. Channel feeds are
        // newest-first, so this also resolves duplicate uploads within one
        // source without weakening the configured source preference.
        if (!key.isEmpty() && titleKeys.contains(key))
            continue;
        videoIds.insert(videoId);
        if (!key.isEmpty())
            titleKeys.insert(key);
        result.append(song);
    }
    return result;
}

bool KaraokeBackend::isValidVideoId(const QString &videoId)
{
    return YouTubePolicy::isValidVideoId(videoId);
}

bool KaraokeBackend::isPlausibleCatalogSize(int candidateItemCount, int cachedItemCount,
                                            int minimumItemCount)
{
    if (candidateItemCount < minimumItemCount)
        return false;
    if (cachedItemCount <= 0)
        return true;

    // A small decrease can reflect removed/private videos. A larger one is far
    // more likely to be a truncated network response and must not replace a
    // healthy cache.
    const int minimumRelativeSize = cachedItemCount - cachedItemCount / 10;
    return candidateItemCount >= qMax(minimumItemCount, minimumRelativeSize);
}

QVariantMap KaraokeBackend::catalogSongFromJson(const QJsonObject &object)
{
    const QString videoId = object.value(QStringLiteral("id")).toString().trimmed();
    const QString rawTitle = object.value(QStringLiteral("title")).toString().trimmed();
    const QString sourceId = sourceIdForObject(object, rawTitle);
    if (!isValidVideoId(videoId) || rawTitle.isEmpty() || sourceId.isEmpty() ||
        isExcludedCatalogTitle(sourceId, rawTitle)) {
        return {};
    }

    QVariantMap song{
        {QStringLiteral("videoId"), videoId},
        {QStringLiteral("rawTitle"), rawTitle},
        {QStringLiteral("displayTitle"), cleanedTitle(rawTitle, sourceId)},
        {QStringLiteral("sourceId"), sourceId},
        {QStringLiteral("url"), QStringLiteral("https://www.youtube.com/watch?v=") + videoId}
    };
    const int durationSeconds = qMax(0, int(object.value(QStringLiteral("duration")).toDouble()));
    if (durationSeconds > 0)
        song[QStringLiteral("durationSeconds")] = durationSeconds;
    return song;
}

void KaraokeBackend::loadCatalog()
{
    m_catalogLoadScheduled = false;
    QElapsedTimer loadTimer;
    loadTimer.start();
    if (!m_catalogLoaded) {
        loadCatalogCache();
        m_catalogLoaded = true;
    }

    if (!m_catalog.isEmpty()) {
        qInfo("[Karaoke] catalog ready with %lld songs in %lld ms",
              m_catalog.size(), loadTimer.elapsed());
        emit catalogChanged(m_catalog.size(), true);
        emit catalogReset(m_catalog, true);
        emit catalogLoadFinished(m_catalog.size(), true);
    }

    if (!m_autoRefreshChecked) {
        m_autoRefreshChecked = true;
        if (m_catalog.isEmpty() || catalogCacheIsStale())
            startCatalogRefresh();
    } else if (m_catalog.isEmpty() && !m_catalogJob->isRunning()) {
        startCatalogRefresh();
    }
}

void KaraokeBackend::loadCatalogDeferred()
{
    if (m_catalogLoadScheduled)
        return;
    m_catalogLoadScheduled = true;
    // Let the module's first frame reach the screen before parsing a large
    // catalog cache. The work itself remains on the owning Qt thread.
    QTimer::singleShot(16, this, &KaraokeBackend::loadCatalog);
}

QVariantMap KaraokeBackend::searchCatalog(const QString &query, int offset,
                                           int limit) const
{
    const QString normalizedQuery = normalizedSearchText(query);
    const QVariantList &items = m_catalogIndexUsesRefreshItems
        ? m_refreshItems : m_catalog;
    const int boundedOffset = qMax(0, offset);
    const int boundedLimit = qBound(1, limit, 500);
    QVariantList matches;
    matches.reserve(boundedLimit);
    int total = 0;
    for (const CatalogSearchEntry &entry : m_catalogSearchIndex) {
        if (!normalizedQuery.isEmpty() && !entry.searchKey.contains(normalizedQuery))
            continue;
        if (total >= boundedOffset && matches.size() < boundedLimit &&
            entry.itemIndex >= 0 && entry.itemIndex < items.size()) {
            matches.append(items.at(entry.itemIndex));
        }
        ++total;
    }
    return {
        {QStringLiteral("items"), matches},
        {QStringLiteral("total"), total},
        {QStringLiteral("catalogTotal"), m_catalogSearchIndex.size()},
        {QStringLiteral("offset"), boundedOffset}
    };
}

void KaraokeBackend::rebuildCatalogIndex(const QVariantList &items,
                                         bool refreshItems)
{
    QVector<CatalogSearchEntry> index;
    index.reserve(items.size());
    for (int itemIndex = 0; itemIndex < items.size(); ++itemIndex) {
        const QVariantMap song = items.at(itemIndex).toMap();
        const QString displayTitle = song.value(QStringLiteral("displayTitle")).toString();
        const QString rawTitle = song.value(QStringLiteral("rawTitle")).toString();
        index.append({itemIndex,
                      normalizedSearchText(displayTitle + QLatin1Char(' ') + rawTitle),
                      articleInsensitiveSortKey(displayTitle),
                      normalizedSearchText(displayTitle),
                      song.value(QStringLiteral("videoId")).toString()});
    }
    std::sort(index.begin(), index.end(), [](const CatalogSearchEntry &left,
                                             const CatalogSearchEntry &right) {
        if (left.sortKey != right.sortKey)
            return left.sortKey < right.sortKey;
        if (left.titleKey != right.titleKey)
            return left.titleKey < right.titleKey;
        return left.videoId < right.videoId;
    });
    m_catalogSearchIndex = std::move(index);
    m_catalogIndexUsesRefreshItems = refreshItems;
}

void KaraokeBackend::refreshCatalog()
{
    if (!m_catalogLoaded) {
        loadCatalogCache();
        m_catalogLoaded = true;
    }
    startCatalogRefresh();
}

QString KaraokeBackend::catalogFilePath() const
{
    return m_dataRoot + QStringLiteral("/karaoke_catalog.json");
}

QString KaraokeBackend::queueFilePath() const
{
    return m_dataRoot + QStringLiteral("/karaoke_queue.json");
}

QString KaraokeBackend::playlistFilePath() const
{
    return m_dataRoot + QStringLiteral("/karaoke_queue.m3u8");
}

QString KaraokeBackend::playbackCacheDirectory() const
{
    return m_dataRoot + QStringLiteral("/karaoke_playback_cache");
}

QString KaraokeBackend::canonicalVideoUrl(const QString &videoId) const
{
    return YouTubePolicy::canonicalVideoUrl(videoId);
}

QString KaraokeBackend::findCachedPlaybackPath(const QString &videoId) const
{
    if (!isValidVideoId(videoId))
        return {};

    const QDir cache(playbackCacheDirectory());
    if (!cache.exists())
        return {};

    const QSet<QString> allowedSuffixes{
        QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
        QStringLiteral("mov")
    };
    const QFileInfoList candidates = cache.entryInfoList(
        {videoId + QStringLiteral(".*")}, QDir::Files | QDir::Readable, QDir::Time);
    const QString cacheCanonical = QFileInfo(cache.absolutePath()).canonicalFilePath();
    for (const QFileInfo &candidate : candidates) {
        if (candidate.completeBaseName() != videoId || candidate.size() <= 1024 ||
            !allowedSuffixes.contains(candidate.suffix().toLower())) {
            continue;
        }
        const QString canonical = candidate.canonicalFilePath();
        if (!cacheCanonical.isEmpty() &&
            canonical.startsWith(cacheCanonical + QDir::separator())) {
            return canonical;
        }
    }
    return {};
}

QString KaraokeBackend::cachedPlaybackPath(const QString &videoId) const
{
    return findCachedPlaybackPath(videoId.trimmed());
}

bool KaraokeBackend::loadCatalogCache()
{
    QFile file(catalogFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;

    const QJsonObject root = document.object();
    const int schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt();
    const bool legacyFunboxCache = schemaVersion == 1 &&
        root.value(QStringLiteral("channelId")).toString() ==
            kCatalogSources.first().channelId;
    QSet<QString> cachedIds;
    const QJsonArray cachedChannelIds = root.value(
        QStringLiteral("sourceChannelIds")).toArray();
    for (const QJsonValue &value : cachedChannelIds)
        cachedIds.insert(value.toString());
    QSet<QString> expectedIds;
    if (schemaVersion >= 2 && schemaVersion <= kCatalogSchemaVersion) {
        for (const CatalogSource &source : kCatalogSources) {
            if (source.introducedInSchema <= schemaVersion)
                expectedIds.insert(source.channelId);
        }
    }
    const bool multiSourceCache = schemaVersion >= 2 &&
                                  schemaVersion <= kCatalogSchemaVersion &&
                                  cachedIds == expectedIds;
    if (!legacyFunboxCache && !multiSourceCache) {
        return false;
    }

    QVariantList loaded;
    QSet<QString> ids;
    const QJsonArray items = root.value(QStringLiteral("items")).toArray();
    loaded.reserve(items.size());
    for (const QJsonValue &value : items) {
        const QVariantMap candidate = value.toObject().toVariantMap();
        const QString videoId = candidate.value(QStringLiteral("videoId")).toString();
        const QString rawTitle = candidate.value(QStringLiteral("rawTitle")).toString().trimmed();
        const QString sourceId = legacyFunboxCache
            ? kFunboxSourceId
            : candidate.value(QStringLiteral("sourceId")).toString();
        if (!isValidVideoId(videoId) || rawTitle.isEmpty() || ids.contains(videoId) ||
            !isKnownSourceId(sourceId) ||
            isExcludedCatalogTitle(sourceId, rawTitle)) {
            continue;
        }
        ids.insert(videoId);
        QVariantMap song{
            {QStringLiteral("videoId"), videoId},
            {QStringLiteral("rawTitle"), rawTitle.left(500)},
            {QStringLiteral("displayTitle"),
             cleanedTitle(rawTitle.left(500), sourceId)},
            {QStringLiteral("sourceId"), sourceId},
            {QStringLiteral("url"), canonicalVideoUrl(videoId)}
        };
        const int duration = candidate.value(QStringLiteral("durationSeconds")).toInt();
        if (duration > 0)
            song[QStringLiteral("durationSeconds")] = duration;
        loaded.append(song);
    }
    if (loaded.isEmpty())
        return false;

    m_catalog = deduplicatedCatalog(loaded);
    rebuildCatalogIndex(m_catalog, false);
    m_catalogSourceCounts = sourceCounts(loaded);
    if (!legacyFunboxCache) {
        const QJsonObject cachedCounts = root.value(
            QStringLiteral("sourceItemCounts")).toObject();
        for (const CatalogSource &source : kCatalogSources) {
            const int visibleCount = m_catalogSourceCounts.value(source.id);
            const int cachedCount = cachedCounts.value(source.id).toInt();
            if (cachedCount >= visibleCount)
                m_catalogSourceCounts[source.id] = cachedCount;
        }
    }
    // Prior catalogs remain useful immediately, but must refresh in the
    // background to add every configured source and write the current schema.
    const bool legacyCache = legacyFunboxCache ||
                             schemaVersion < kCatalogSchemaVersion;
    m_catalogFetchedAt = legacyCache
        ? QDateTime{}
        : QDateTime::fromString(
              root.value(QStringLiteral("fetchedAt")).toString(), Qt::ISODate);
    return true;
}

bool KaraokeBackend::saveCatalogCache() const
{
    QJsonArray items;
    for (const QVariant &item : m_catalog)
        items.append(QJsonObject::fromVariantMap(item.toMap()));

    QJsonArray sourceChannelIds;
    for (const CatalogSource &source : kCatalogSources)
        sourceChannelIds.append(source.channelId);
    QJsonObject sourceItemCounts;
    for (const CatalogSource &source : kCatalogSources)
        sourceItemCounts[source.id] = m_catalogSourceCounts.value(source.id);

    const QJsonObject root{
        {QStringLiteral("schemaVersion"), kCatalogSchemaVersion},
        {QStringLiteral("sourceChannelIds"), sourceChannelIds},
        {QStringLiteral("sourceItemCounts"), sourceItemCounts},
        {QStringLiteral("fetchedAt"), m_catalogFetchedAt.toString(Qt::ISODate)},
        {QStringLiteral("ytDlpVersion"), QStringLiteral(BUNDLED_YT_DLP_VERSION)},
        {QStringLiteral("items"), items}
    };

    QSaveFile file(catalogFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool KaraokeBackend::catalogCacheIsStale() const
{
    if (!m_catalogFetchedAt.isValid())
        return true;
    return m_catalogFetchedAt.secsTo(QDateTime::currentDateTimeUtc())
        >= kCatalogRefreshHours * 60 * 60;
}

bool KaraokeBackend::catalogSourcesArePlausible() const
{
    for (const CatalogSource &source : kCatalogSources) {
        if (!isPlausibleCatalogSize(m_refreshSourceCounts.value(source.id),
                                    m_catalogSourceCounts.value(source.id),
                                    source.minimumItems)) {
            return false;
        }
    }
    return true;
}

void KaraokeBackend::startCatalogRefresh()
{
    if (m_catalogJob->isRunning())
        return;

    QStringList sourceUrls;
    sourceUrls.reserve(int(std::size(kCatalogSources)));
    for (const CatalogSource &source : kCatalogSources)
        sourceUrls.append(source.url);
    const QString ytDlp = HelperResolver::ytDlp(m_appRoot);
    const QStringList arguments = YouTubePolicy::playlistInventoryArguments(
        m_appRoot, sourceUrls);
    if (ytDlp.isEmpty() || arguments.isEmpty()) {
        emit catalogLoadFailed(QStringLiteral("Bundled YouTube helpers are unavailable."),
                               !m_catalog.isEmpty());
        return;
    }

    m_catalogOutputBuffer.clear();
    m_refreshItems.clear();
    m_refreshBatch.clear();
    m_refreshIds.clear();
    m_refreshPreferredPriorityByKey.clear();
    m_refreshSourceCounts.clear();
    m_refreshHadCache = !m_catalog.isEmpty();

    emit catalogLoadStarted(m_refreshHadCache);
    // The registry order is the duplicate preference order, allowing cold-load
    // batches to suppress lower-ranked equivalents before they reach the UI.
    // Final reconciliation applies the same ranking independent of fetch order.
    if (!m_catalogJob->start(arguments, 10 * 60 * 1000,
                             96LL * 1024LL * 1024LL, kMaxHelperErrorBytes)) {
        finishCatalogRefresh(YouTubeJob::Failure::Unavailable, -1, {});
    }
}

void KaraokeBackend::consumeCatalogOutput(bool includeRemainder)
{
    int newline = -1;
    while ((newline = m_catalogOutputBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_catalogOutputBuffer.left(newline).trimmed();
        m_catalogOutputBuffer.remove(0, newline + 1);
        consumeCatalogLine(line);
    }
    if (includeRemainder && !m_catalogOutputBuffer.trimmed().isEmpty()) {
        consumeCatalogLine(m_catalogOutputBuffer.trimmed());
        m_catalogOutputBuffer.clear();
    }
}

void KaraokeBackend::consumeCatalogLine(const QByteArray &line)
{
    if (line.isEmpty())
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QVariantMap song = catalogSongFromJson(document.object());
    const QString videoId = song.value(QStringLiteral("videoId")).toString();
    if (song.isEmpty() || m_refreshIds.contains(videoId))
        return;
    m_refreshIds.insert(videoId);
    const QString sourceId = song.value(QStringLiteral("sourceId")).toString();
    ++m_refreshSourceCounts[sourceId];
    const QString key = deduplicationKey(song.value(QStringLiteral("displayTitle")).toString());
    if (!key.isEmpty()) {
        const int priority = sourcePriority(sourceId);
        const auto preferred = m_refreshPreferredPriorityByKey.constFind(key);
        if (preferred != m_refreshPreferredPriorityByKey.cend() &&
            preferred.value() < priority) {
            return;
        }
        if (preferred == m_refreshPreferredPriorityByKey.cend() ||
            priority < preferred.value()) {
            m_refreshPreferredPriorityByKey[key] = priority;
        }
    }
    m_refreshItems.append(song);
    m_refreshBatch.append(song);
    if (m_refreshBatch.size() >= kCatalogBatchSize)
        flushCatalogBatch();
}

void KaraokeBackend::flushCatalogBatch()
{
    if (m_refreshBatch.isEmpty())
        return;
    if (!m_refreshHadCache) {
        rebuildCatalogIndex(m_refreshItems, true);
        emit catalogChanged(m_refreshItems.size(), false);
        if (m_refreshItems.size() == m_refreshBatch.size())
            emit catalogReset(m_refreshBatch, false);
        else
            emit catalogItemsAppended(m_refreshBatch);
    }
    m_refreshBatch.clear();
}

void KaraokeBackend::finishCatalogRefresh(YouTubeJob::Failure failure, int exitCode,
                                           const QString &safeError)
{
    Q_UNUSED(exitCode)
    consumeCatalogOutput(true);
    flushCatalogBatch();

    const bool helperSucceeded = failure == YouTubeJob::Failure::None;
    const QVariantList reconciledItems = deduplicatedCatalog(m_refreshItems);
    const bool catalogSizeIsPlausible = isPlausibleCatalogSize(
        reconciledItems.size(), m_catalog.size());
    const bool sourcesArePlausible = catalogSourcesArePlausible();
    const bool successful = helperSucceeded && catalogSizeIsPlausible &&
                            sourcesArePlausible;
    if (successful) {
        const bool progressiveItemsChanged = reconciledItems.size() != m_refreshItems.size();
        m_catalog = reconciledItems;
        rebuildCatalogIndex(m_catalog, false);
        m_catalogSourceCounts = m_refreshSourceCounts;
        m_catalogFetchedAt = QDateTime::currentDateTimeUtc();
        saveCatalogCache();
        if (m_refreshHadCache || progressiveItemsChanged)
            emit catalogReset(m_catalog, false);
        emit catalogChanged(m_catalog.size(), false);
        emit catalogLoadFinished(m_catalog.size(), false);
    } else {
        QString detail = safeError;
        QString message = QStringLiteral("Could not refresh the Karaoke catalog.");
        if (helperSucceeded && (!catalogSizeIsPlausible || !sourcesArePlausible))
            detail = QStringLiteral("The catalog response was incomplete.");
        if (!detail.isEmpty())
            message += QStringLiteral(" ") + detail;
        if (!m_refreshHadCache && !m_refreshItems.isEmpty()) {
            m_catalogSearchIndex.clear();
            m_catalogIndexUsesRefreshItems = false;
            emit catalogReset(QVariantList{}, false);
            emit catalogChanged(0, false);
        }
        emit catalogLoadFailed(message, !m_catalog.isEmpty());
    }

    m_refreshItems.clear();
    m_refreshBatch.clear();
    m_refreshIds.clear();
    m_refreshPreferredPriorityByKey.clear();
    m_refreshSourceCounts.clear();
}

QVariantList KaraokeBackend::getQueue() const
{
    return m_queue;
}

QVariantMap KaraokeBackend::validatedQueueEntry(const QVariantMap &candidate,
                                                bool createEntryId) const
{
    const QString videoId = candidate.value(QStringLiteral("videoId")).toString().trimmed();
    const QString rawTitle = candidate.value(QStringLiteral("rawTitle")).toString().trimmed();
    if (!isValidVideoId(videoId) || rawTitle.isEmpty())
        return {};
    const QString sourceId = candidate.value(QStringLiteral("sourceId")).toString();
    const bool hasKnownSource = isKnownSourceId(sourceId);

    QString entryId = candidate.value(QStringLiteral("entryId")).toString().trimmed();
    if (entryId.isEmpty() && createEntryId)
        entryId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (entryId.length() != 36 || QUuid(entryId).isNull())
        return {};

    const QString requestedStatus = candidate.value(
        QStringLiteral("status"), QStringLiteral("queued")).toString();
    const QString status = requestedStatus == QStringLiteral("failed")
        ? QStringLiteral("failed") : QStringLiteral("queued");

    QVariantMap entry{
        {QStringLiteral("entryId"), entryId},
        {QStringLiteral("videoId"), videoId},
        {QStringLiteral("rawTitle"), rawTitle},
        {QStringLiteral("displayTitle"),
         cleanedTitle(rawTitle, hasKnownSource ? sourceId : QString{})},
        {QStringLiteral("url"), canonicalVideoUrl(videoId)},
        {QStringLiteral("status"), status}
    };
    if (hasKnownSource)
        entry[QStringLiteral("sourceId")] = sourceId;
    const int duration = candidate.value(QStringLiteral("durationSeconds")).toInt();
    if (duration > 0)
        entry[QStringLiteral("durationSeconds")] = duration;
    const QString error = candidate.value(QStringLiteral("error")).toString().trimmed();
    if (status == QStringLiteral("failed") && !error.isEmpty())
        entry[QStringLiteral("error")] = safeErrorText(error.toUtf8()).left(300);
    return entry;
}

void KaraokeBackend::loadQueue()
{
    QFile file(queueFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return;
    const QJsonObject root = document.object();
    if (root.value(QStringLiteral("schemaVersion")).toInt() != kQueueSchemaVersion)
        return;

    QSet<QString> entryIds;
    for (const QJsonValue &value : root.value(QStringLiteral("entries")).toArray()) {
        QVariantMap entry = validatedQueueEntry(value.toObject().toVariantMap(), false);
        const QString entryId = entry.value(QStringLiteral("entryId")).toString();
        if (entry.isEmpty() || entryIds.contains(entryId))
            continue;
        entryIds.insert(entryId);
        m_queue.append(entry);
    }
}

bool KaraokeBackend::saveQueue() const
{
    QJsonArray entries;
    for (const QVariant &item : m_queue)
        entries.append(QJsonObject::fromVariantMap(item.toMap()));
    const QJsonObject root{
        {QStringLiteral("schemaVersion"), kQueueSchemaVersion},
        {QStringLiteral("entries"), entries}
    };
    QSaveFile file(queueFilePath());
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

int KaraokeBackend::queueIndexForEntryId(const QString &entryId) const
{
    for (int index = 0; index < m_queue.size(); ++index) {
        if (m_queue.at(index).toMap().value(QStringLiteral("entryId")).toString() == entryId)
            return index;
    }
    return -1;
}

bool KaraokeBackend::publishQueue()
{
    if (!saveQueue())
        return false;
    emit queueChanged(m_queue);
    return true;
}

bool KaraokeBackend::enqueue(const QVariantMap &song)
{
    const QVariantMap entry = validatedQueueEntry(song, true);
    if (entry.isEmpty())
        return false;
    m_queue.append(entry);
    if (publishQueue())
        return true;
    m_queue.removeLast();
    return false;
}

bool KaraokeBackend::removeQueueEntry(const QString &entryId)
{
    const int index = queueIndexForEntryId(entryId);
    if (index < 0)
        return false;
    const QVariant removed = m_queue.takeAt(index);
    if (publishQueue())
        return true;
    m_queue.insert(index, removed);
    return false;
}

bool KaraokeBackend::moveQueueEntry(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_queue.size() ||
        toIndex < 0 || toIndex >= m_queue.size() || fromIndex == toIndex) {
        return false;
    }
    m_queue.move(fromIndex, toIndex);
    if (publishQueue())
        return true;
    m_queue.move(toIndex, fromIndex);
    return false;
}

void KaraokeBackend::clearQueue()
{
    if (m_queue.isEmpty())
        return;
    const QVariantList previous = m_queue;
    m_queue.clear();
    if (!publishQueue())
        m_queue = previous;
}

void KaraokeBackend::clearQueueExcept(const QString &entryId)
{
    const int index = queueIndexForEntryId(entryId);
    QVariantList retained;
    if (index >= 0)
        retained.append(m_queue.at(index));
    if (retained == m_queue)
        return;
    const QVariantList previous = m_queue;
    m_queue = retained;
    if (!publishQueue())
        m_queue = previous;
}

bool KaraokeBackend::completeQueueEntry(const QString &entryId)
{
    return removeQueueEntry(entryId);
}

bool KaraokeBackend::failQueueEntry(const QString &entryId, const QString &message)
{
    const int index = queueIndexForEntryId(entryId);
    if (index < 0)
        return false;
    const QVariantMap previous = m_queue.at(index).toMap();
    QVariantMap entry = previous;
    entry[QStringLiteral("status")] = QStringLiteral("failed");
    entry[QStringLiteral("error")] = safeErrorText(message.toUtf8()).left(300);
    m_queue[index] = entry;
    if (publishQueue())
        return true;
    m_queue[index] = previous;
    return false;
}

bool KaraokeBackend::resetQueueEntry(const QString &entryId)
{
    const int index = queueIndexForEntryId(entryId);
    if (index < 0)
        return false;
    const QVariantMap previous = m_queue.at(index).toMap();
    QVariantMap entry = previous;
    entry[QStringLiteral("status")] = QStringLiteral("queued");
    entry.remove(QStringLiteral("error"));
    m_queue[index] = entry;
    if (publishQueue())
        return true;
    m_queue[index] = previous;
    return false;
}

void KaraokeBackend::cleanupPrefetchArtifacts(const QString &videoId) const
{
    if (!isValidVideoId(videoId))
        return;
    QDir cache(playbackCacheDirectory());
    const QFileInfoList artifacts = cache.entryInfoList(
        {videoId + QStringLiteral(".*")}, QDir::Files | QDir::Hidden);
    for (const QFileInfo &artifact : artifacts)
        QFile::remove(artifact.absoluteFilePath());
}

void KaraokeBackend::stopPrefetchProcess(bool removeArtifacts)
{
    if (m_prefetchJob)
        m_prefetchJob->cancelSilently();
    if (removeArtifacts)
        cleanupPrefetchArtifacts(m_prefetchVideoId);
    m_prefetchEntryId.clear();
    m_prefetchVideoId.clear();
}

void KaraokeBackend::prefetchQueueEntry(const QString &entryId)
{
    const int queueIndex = queueIndexForEntryId(entryId);
    if (queueIndex < 0)
        return;
    const QString videoId = m_queue.at(queueIndex).toMap()
                                .value(QStringLiteral("videoId")).toString();
    if (!isValidVideoId(videoId))
        return;

    const QString cached = findCachedPlaybackPath(videoId);
    if (!cached.isEmpty()) {
        QTimer::singleShot(0, this, [this, entryId, cached] {
            emit queueEntryPrefetched(entryId, cached);
        });
        return;
    }

    if (m_prefetchJob->isRunning()) {
        if (m_prefetchVideoId == videoId) {
            m_prefetchEntryId = entryId;
            return;
        }
        stopPrefetchProcess(true);
    }

    if (!QDir().mkpath(playbackCacheDirectory())) {
        emit queueEntryPrefetchFailed(
            entryId, QStringLiteral("Could not create the Karaoke playback cache."));
        return;
    }

    cleanupPrefetchArtifacts(videoId);
    m_prefetchEntryId = entryId;
    m_prefetchVideoId = videoId;

    emit queueEntryPrefetchStarted(entryId);
    const QString outputTemplate = QDir(playbackCacheDirectory()).filePath(
        videoId + QStringLiteral(".%(ext)s"));
    const QStringList arguments = YouTubePolicy::videoPrefetchArguments(
        m_appRoot, outputTemplate, videoId);
    if (arguments.isEmpty() ||
        !m_prefetchJob->start(arguments, 15 * 60 * 1000,
                              1024 * 1024, kMaxHelperErrorBytes)) {
        stopPrefetchProcess(true);
        emit queueEntryPrefetchFailed(
            entryId, QStringLiteral("Bundled Karaoke prefetch helpers are unavailable."));
        return;
    }
}

void KaraokeBackend::finishPrefetch(YouTubeJob::Failure failure, int exitCode,
                                    const QString &safeError)
{
    Q_UNUSED(exitCode)
    if (m_prefetchEntryId.isEmpty())
        return;

    const QString entryId = m_prefetchEntryId;
    const QString videoId = m_prefetchVideoId;
    const bool helperSucceeded = failure == YouTubeJob::Failure::None;
    const QString cached = helperSucceeded ? findCachedPlaybackPath(videoId) : QString{};

    m_prefetchEntryId.clear();
    m_prefetchVideoId.clear();

    if (!cached.isEmpty()) {
        QFile::setPermissions(cached, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        prunePlaybackCache();
        emit queueEntryPrefetched(entryId, cached);
        return;
    }

    cleanupPrefetchArtifacts(videoId);
    const QString detail = safeError;
    QString message = QStringLiteral("Could not prepare the next Karaoke song.");
    if (!detail.isEmpty())
        message += QStringLiteral(" ") + detail;
    emit queueEntryPrefetchFailed(entryId, message);
}

void KaraokeBackend::prunePlaybackCache()
{
    QDir cache(playbackCacheDirectory());
    QFileInfoList files;
    qint64 totalBytes = 0;
    for (const QFileInfo &info : cache.entryInfoList(
             QDir::Files | QDir::NoDotAndDotDot, QDir::Time | QDir::Reversed)) {
        if (findCachedPlaybackPath(info.completeBaseName()) != info.canonicalFilePath())
            continue;
        files.append(info);
        totalBytes += info.size();
    }

    QSet<QString> protectedVideoIds;
    for (const QVariant &item : m_queue)
        protectedVideoIds.insert(item.toMap().value(QStringLiteral("videoId")).toString());

    int fileCount = files.size();
    for (const QFileInfo &info : files) {
        if (fileCount <= kMaxCachedPlaybackFiles && totalBytes <= kMaxCachedPlaybackBytes)
            break;
        if (protectedVideoIds.contains(info.completeBaseName()))
            continue;
        if (QFile::remove(info.absoluteFilePath())) {
            --fileCount;
            totalBytes -= info.size();
        }
    }
}

QString KaraokeBackend::writePlaybackPlaylist()
{
    QSaveFile file(playlistFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return {};
    file.write("#EXTM3U\n");
    int written = 0;
    for (const QVariant &item : m_queue) {
        const QString videoId = item.toMap().value(QStringLiteral("videoId")).toString();
        const QString cached = findCachedPlaybackPath(videoId);
        const QString url = cached.isEmpty() ? canonicalVideoUrl(videoId) : cached;
        if (url.isEmpty())
            continue;
        file.write(url.toUtf8());
        file.write("\n");
        ++written;
    }
    if (written == 0 || !file.commit())
        return {};
    QFile::setPermissions(playlistFilePath(),
                          QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    return playlistFilePath();
}
