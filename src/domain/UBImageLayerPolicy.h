#ifndef UBIMAGELAYERPOLICY_H
#define UBIMAGELAYERPOLICY_H

#include <QGraphicsItem>
#include <QVector>
#include <algorithm>

namespace UBImageLayerPolicy
{
enum Placement { BelowInk, AboveInk, Up, Down, Top, Bottom };
struct Entry { QGraphicsItem* item; qreal z; bool ink; };

// Pure ordering policy, shared by production and regression tests. Use exact
// integer ranks so old SVG documents/readers retain the order without float
// rounding collisions. Layers contain content roots only, never controls.
inline QVector<Entry> ordered(QVector<Entry> entries)
{
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.z < b.z; });
    return entries;
}

inline QVector<Entry> plan(QVector<Entry> entries, QGraphicsItem* image, Placement placement)
{
    entries = ordered(entries);
    int oldIndex = -1;
    for (int i = 0; i < entries.size(); ++i)
        if (entries[i].item == image) { oldIndex = i; break; }
    if (oldIndex < 0 || entries.size() >= 999999) return {};
    const Entry moved = entries.takeAt(oldIndex);
    int target = oldIndex;
    switch (placement) {
    case BelowInk:
        target = entries.size();
        for (int i = 0; i < entries.size(); ++i)
            if (entries[i].ink) { target = i; break; }
        break;
    case AboveInk:
        target = entries.size();
        for (int i = entries.size()-1; i >= 0; --i)
            if (entries[i].ink) { target = i+1; break; }
        break;
    case Up: target = std::min(oldIndex+1, int(entries.size())); break;
    case Down: target = std::max(oldIndex-1, 0); break;
    case Top: target = entries.size(); break;
    case Bottom: target = 0; break;
    }
    entries.insert(target, moved);
    for (int i = 0; i < entries.size(); ++i) entries[i].z = -999999.0 + i;
    return entries;
}
}
#endif
