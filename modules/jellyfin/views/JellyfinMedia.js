.pragma library

function durationStr(ms) {
    if (!ms) return ""
    var totalMin = Math.floor(ms / 60000)
    var h = Math.floor(totalMin / 60)
    var m = totalMin % 60
    if (h > 0) return h + "HR:" + (m < 10 ? "0" : "") + m + "MIN"
    return m + "MIN"
}

function breadcrumbText(parts) {
    var out = []
    for (var i = 0; i < parts.length; i++) {
        if (parts[i]) out.push(parts[i])
    }
    return out.join(" / ")
}

function searchText(item) {
    return [
        item.title || "",
        item.name || "",
        item.seriesName || "",
        item.seasonName || "",
        item.episodeCode || "",
        item.year ? String(item.year) : ""
    ].join(" ")
}

function primaryTitle(item) {
    if (!item) return ""
    if (item.type === "episode" && item.seriesName)
        return item.seriesName
    return item.title || item.name || ""
}

function metadataLine(item) {
    if (!item) return ""

    var parts = []
    if (item.type === "episode") {
        var epTitle = item.name || item.title || ""
        if (item.episodeCode && epTitle)
            parts.push(item.episodeCode + ": " + epTitle)
        else if (item.episodeCode)
            parts.push(item.episodeCode)
        else if (epTitle)
            parts.push(epTitle)
    } else if (item.type === "season") {
        if (item.title) parts.push(item.title)
    } else if (item.year) {
        parts.push(String(item.year))
    }

    if (item.type === "episode" && item.year)
        parts.push(String(item.year))
    if (item.duration)
        parts.push(durationStr(item.duration))

    return parts.join(" - ")
}

function countLabel(count, singular, plural) {
    if (!count || count < 1) return ""
    return count + " " + (count === 1 ? singular : plural)
}

function seriesMetadataLine(item, loadedSeasonCount) {
    if (!item) return ""

    var parts = []
    if (item.year) parts.push(String(item.year))
    if (loadedSeasonCount)
        parts.push(countLabel(loadedSeasonCount, "SEASON", "SEASONS"))
    else if (item.childCount)
        parts.push(countLabel(item.childCount, "SEASON", "SEASONS"))
    if (item.recursiveItemCount)
        parts.push(countLabel(item.recursiveItemCount, "EPISODE", "EPISODES"))
    if (item.status) parts.push(String(item.status).toUpperCase())
    if (item.officialRating) parts.push(String(item.officialRating).toUpperCase())
    if (item.communityRating)
        parts.push(Number(item.communityRating).toFixed(1) + "/10")
    return parts.join(" - ")
}

function genreLine(item) {
    if (!item || !item.genres || item.genres.length === 0)
        return ""
    var genres = []
    for (var i = 0; i < item.genres.length && i < 4; i++)
        genres.push(String(item.genres[i]).toUpperCase())
    return genres.join(" / ")
}

function playbackTitle(item) {
    if (!item) return ""
    if (item.type === "episode" && item.seriesName) {
        var episode = item.name || item.title || ""
        if (item.episodeCode && episode)
            return item.seriesName + " - " + item.episodeCode + " - " + episode
        if (episode)
            return item.seriesName + " - " + episode
        return item.seriesName
    }
    return item.title || item.name || ""
}

function listTitle(item, includeSeries) {
    if (!item) return ""
    if (item.type !== "episode") return item.title || item.name || ""

    var title = item.name || item.title || ""
    var prefix = item.episodeCode || ""
    if (includeSeries && item.seriesName)
        prefix = item.seriesName + (prefix ? " - " + prefix : "")
    return prefix ? prefix + ": " + title : title
}

function watchedPrefix(item) {
    if (item.type === "series" || item.type === "season") {
        if (item.unplayedItemCount > 0) return "[" + item.unplayedItemCount + " LEFT] "
    } else if (item.type !== "movie" && item.type !== "episode") {
        return ""
    }
    return item.played === true ? "[WATCHED] " : ""
}

function buildSubtitleStreams(item) {
    var subs = [{ "id": "-1", "mpvTrack": -1, "displayTitle": "OFF", "subFile": "" }]
    var loaded = item && item.subtitleStreams ? item.subtitleStreams : []
    for (var i = 0; i < loaded.length; i++)
        subs.push(loaded[i])
    return subs
}

function defaultAudioIndex(audioStreams) {
    if (!audioStreams || audioStreams.length === 0)
        return 0
    for (var i = 0; i < audioStreams.length; i++) {
        if (audioStreams[i].isDefault)
            return i
    }
    return 0
}

function preferredAudioIndex(audioStreams, language) {
    if (language) {
        for (var i = 0; i < audioStreams.length; i++) {
            if ((audioStreams[i].language || "").toLowerCase() === language.toLowerCase())
                return i
        }
    }
    return defaultAudioIndex(audioStreams)
}

function preferredSubtitleIndex(subtitleStreams, language) {
    if (!language || language === "__off__") return 0
    for (var i = 1; i < subtitleStreams.length; i++) {
        if ((subtitleStreams[i].language || "").toLowerCase() === language.toLowerCase())
            return i
    }
    return 0
}

function selectedAudioStreamIndex(audioStreams, audioIdx) {
    var selected = audioStreams && audioStreams[audioIdx]
    return selected && selected.streamIndex !== undefined ? selected.streamIndex : -1
}

function selectedSubtitleStreamIndex(subtitleStreams, subtitleIdx) {
    var selected = subtitleStreams && subtitleStreams[subtitleIdx]
    return selected && selected.streamIndex !== undefined ? selected.streamIndex : -1
}

function selectedLanguage(streams, index, offValue) {
    var selected = streams && streams[index]
    return selected ? (selected.language || "") : (offValue || "")
}

function maxTrackFocusRow(audioStreams, subtitleStreams) {
    var maxRow = 0
    if (audioStreams && audioStreams.length > 0)
        maxRow = 1
    if (subtitleStreams && subtitleStreams.length > 1)
        maxRow = 2
    return maxRow
}

function selectedAudioTrack(audioStreams, audioIdx) {
    if (!audioStreams || !audioStreams[audioIdx])
        return 0
    return audioStreams[audioIdx].mpvTrack || 0
}

function selectedSubtitleTrack(subtitleStreams, subtitleIdx) {
    var selected = subtitleStreams && subtitleStreams[subtitleIdx]
    if (!selected)
        return -1
    if (selected.subFile && selected.subFile !== "")
        return 0
    if (selected.mpvTrack !== undefined && selected.mpvTrack !== null)
        return selected.mpvTrack
    return -1
}

function selectedSubtitleFiles(subtitleStreams, subtitleIdx) {
    var selected = subtitleStreams && subtitleStreams[subtitleIdx]
    if (selected && selected.subFile && selected.subFile !== "")
        return [selected.subFile]
    return []
}
