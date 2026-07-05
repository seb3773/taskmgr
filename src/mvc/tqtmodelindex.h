#ifndef TQTMODELINDEX_H
#define TQTMODELINDEX_H

/**
 * @brief Lightweight model index for TQt MVC item models.
 *
 * Uses a stable nodeId rather than a flat row index so selection and view
 * state survive structural changes (insert/remove) during refresh cycles.
 */
struct TQtModelIndex
{
    int nodeId;   ///< Stable ID in TQtTreeStore (-1 = invalid). For flat list models, equals row index.
    int column;   ///< Column index

    TQtModelIndex() : nodeId(-1), column(0) {}

    TQtModelIndex(int id, int col = 0) : nodeId(id), column(col) {}

    bool isValid() const { return nodeId >= 0; }

    bool operator==(const TQtModelIndex& o) const {
        return nodeId == o.nodeId && column == o.column;
    }

    bool operator!=(const TQtModelIndex& o) const {
        return !(*this == o);
    }
};

#endif // TQTMODELINDEX_H
