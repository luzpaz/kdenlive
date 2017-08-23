/*
Copyright (C) 2017  Jean-Baptiste Mardelle <jb@kdenlive.org>
This file is part of Kdenlive. See www.kdenlive.org.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of
the License or (at your option) version 3 or any later version
accepted by the membership of KDE e.V. (or its successor approved
by the membership of KDE e.V.), which shall act as a proxy
defined in Section 14 of version 3 of the license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "timelinefunctions.hpp"
#include "clipmodel.hpp"
#include "groupsmodel.hpp"
#include "core.h"
#include "timelineitemmodel.hpp"
#include "effects/effectstack/model/effectstackmodel.hpp"

#include <QDebug>
#include <klocalizedstring.h>

bool TimelineFunctions::copyClip(std::shared_ptr<TimelineItemModel> timeline, int clipId, int &newId, Fun &undo, Fun &redo)
{
    bool res = timeline->requestClipCreation(timeline->getClipBinId(clipId), newId, undo, redo);
    int duration = timeline->getClipPlaytime(clipId);
    int init_duration = timeline->getClipPlaytime(newId);
    if (duration != init_duration) {
        int in = timeline->m_allClips[clipId]->getIn();
        res = res && timeline->requestItemResize(newId, init_duration - in, false, true, undo, redo);
        res = res && timeline->requestItemResize(newId, duration, true, true, undo, redo);
    }
    if (!res) {
        return false;
    }
    std::shared_ptr<EffectStackModel> sourceStack = timeline->getClipEffectStackModel(clipId);
    std::shared_ptr<EffectStackModel> destStack = timeline->getClipEffectStackModel(newId);
    destStack->importEffects(sourceStack);
    return res;
}

bool TimelineFunctions::requestClipCut(std::shared_ptr<TimelineItemModel> timeline, int clipId, int position, int &newId, Fun &undo, Fun &redo)
{
    int start = timeline->getClipPosition(clipId);
    int duration = timeline->getClipPlaytime(clipId);
    if (start > position || (start + duration) < position) {
        return false;
    }
    bool res = copyClip(timeline, clipId, newId, undo, redo);
    res = res && timeline->requestItemResize(clipId, position - start, true, true, undo, redo);
    int newDuration = timeline->getClipPlaytime(clipId);
    res = res && timeline->requestItemResize(newId, duration - newDuration, true, true, undo, redo);
    res = res && timeline->requestClipMove(newId, timeline->getClipTrackId(clipId), position, true, undo, redo);
    return res;
}
bool TimelineFunctions::requestClipCut(std::shared_ptr<TimelineItemModel> timeline, int clipId, int position)
{
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    const std::unordered_set<int> clips = timeline->getGroupElements(clipId);
    int count = 0;
    for (int cid : clips) {
        int start = timeline->getClipPosition(cid);
        int duration = timeline->getClipPlaytime(cid);
        if (start < position && (start + duration) > position) {
            count++;
            int newId;
            bool res = requestClipCut(timeline, cid, position, newId, undo, redo);
            if (!res) {
                bool undone = undo();
                Q_ASSERT(undone);
                return false;
            }
            // splitted elements go temporarily in the same group as original ones.
            timeline->m_groups->setInGroupOf(newId, cid, undo, redo);
        }
    }
    if (count > 0 && timeline->m_groups->isInGroup(clipId)) {
        // we now split the group hiearchy.
        // As a splitting criterion, we compare start point with split position
        auto criterion = [timeline, position](int cid) {
            return timeline->getClipPosition(cid) < position;
        };
        int root = timeline->m_groups->getRootId(clipId);
        bool res = timeline->m_groups->split(root, criterion, undo, redo);
        if (!res) {
            bool undone = undo();
            Q_ASSERT(undone);
            return false;
        }
    }
    if (count > 0) {
        pCore->pushUndo(undo, redo, i18n("Cut clip"));
    }
    return count > 0;
}

int TimelineFunctions::requestSpacerStartOperation(std::shared_ptr<TimelineItemModel> timeline, int trackId, int position)
{
    std::unordered_set<int> clips = timeline->getItemsAfterPosition(trackId, position, -1);
    if (clips.size() > 0) {
        timeline->requestClipsGroup(clips, false);
        return (*clips.cbegin());
    }
    return -1;
}

bool TimelineFunctions::requestSpacerEndOperation(std::shared_ptr<TimelineItemModel> timeline, int clipId, int startPosition, int endPosition)
{
    // Move group back to original position
    int track = timeline->getItemTrackId(clipId);
    timeline->requestClipMove(clipId, track, startPosition, false, false);
    std::unordered_set<int> clips = timeline->getGroupElements(clipId);
    // break group
    timeline->requestClipUngroup(clipId, false);
    // Start undoable command
    std::function<bool(void)> undo = []() { return true; };
    std::function<bool(void)> redo = []() { return true; };
    int res = timeline->requestClipsGroup(clips, undo, redo);
    bool final = false;
    if (res > -1) {
        if (clips.size() > 1) {
            final = timeline->requestGroupMove(clipId, res, 0, endPosition - startPosition, true, undo, redo);
        } else {
            // only 1 clip to be moved
            final = timeline->requestClipMove(clipId, track, endPosition, true, undo, redo);
        }
    }
    if (final && clips.size() > 1) {
        final = timeline->requestClipUngroup(clipId, undo, redo);
    }
    if (final) {
        pCore->pushUndo(undo, redo, i18n("Insert space"));
        return true;
    }
    return false;
}
