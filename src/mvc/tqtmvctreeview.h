#ifndef TQTMVCTREEVIEW_H
#define TQTMVCTREEVIEW_H

#include <ntqtable.h>
#include <ntqvaluevector.h>
#include <ntqpoint.h>
#include <ntqmap.h>
#include "tqtabstractitemmodel.h"
#include "tqtmodelindex.h"

/**
 * @brief A high-performance virtual tree table view driven by TQtAbstractItemModel.
 *
 * Flattens expanded nodes into display rows for virtual rendering on TQTable.
 * Expand/collapse toggles are painted in column 0; click handling is separate
 * from row selection.
 */
class TQLabel;
class TQTimer;
class TQFrame;

class TQtMvcTreeView : public TQTable
{
    TQ_OBJECT

public:
    static const int IndentPerLevel = 16;
    static const int ToggleSize = 12;

    TQtMvcTreeView(TQWidget* parent = 0);
    ~TQtMvcTreeView();

    void blockPainting() { m_blockPaintingDepth++; }
    void unblockPainting() {
        if (m_blockPaintingDepth > 0) {
            m_blockPaintingDepth--;
            if (m_blockPaintingDepth == 0 && viewport()) {
                viewport()->update();
            }
        }
    }

    void setExpandedNoRebuild(int nodeId, bool expanded) {
        if (expanded)
            m_expanded[nodeId] = true;
        else
            m_expanded.remove(nodeId);
    }

    void setModel(TQtAbstractItemModel* model);
    TQtAbstractItemModel* model() const;

    void setSortingEnabled(bool enabled);
    void sortByColumn(int column, bool ascending = true);
    int sortColumn() const { return m_sortColumn; }
    bool sortAscending() const { return m_sortAscending; }

    /** @brief Returns the nodeId for a display row, or -1. */
    int nodeId(int displayRow) const;

    /** @brief Returns depth (0 = root) for a display row. */
    int depthAt(int displayRow) const;

    TQtModelIndex selectedIndex() const;
    void selectIndex(const TQtModelIndex& index);
    void ensureIndexVisible(const TQtModelIndex& index);

    bool isExpanded(int nodeId) const;
    void setExpanded(int nodeId, bool expanded);
    void expand(const TQtModelIndex& index);
    void collapse(const TQtModelIndex& index);
    void toggleExpanded(int nodeId);
    int displayRowForNode(int nodeId) const;

public slots:
    void setFilterText(const TQString& text);
    void setColumnFilter(int column, const TQString& matchValue);
    void expandAll();
    void collapseAll();

signals:
    void selectionChanged(const TQtModelIndex& index);
    void rowContextMenuRequested(const TQtModelIndex& index, int column, const TQPoint& globalPos);
    void expanded(int nodeId);
    void collapsed(int nodeId);

protected:
    void paintCell(TQPainter* p, int row, int col, const TQRect& cr, bool selected);
    void paintEmptyArea(TQPainter* p, int cx, int cy, int cw, int ch);
    void viewportPaintEvent(TQPaintEvent* e);
    TQTableItem* item(int row, int col) const { return 0; }
    void currentChanged(int row, int col);
    void contentsContextMenuEvent(TQContextMenuEvent* e);
    void contentsMousePressEvent(TQMouseEvent* e);
    void keyPressEvent(TQKeyEvent* e);
    void resizeEvent(TQResizeEvent* e);
    bool eventFilter(TQObject* watched, TQEvent* event);

private slots:
    void onIndexDataChanged(const TQtModelIndex& topLeft, const TQtModelIndex& bottomRight);
    void onIndexRowsInserted(const TQtModelIndex& parent, int first, int count);
    void onIndexRowsRemoved(const TQtModelIndex& parent, int first, int count);
    void onModelReset();
    void onLayoutChanged();
    void onHeaderClicked(int col);
    void hideSearch();

private:
    struct VisibleEntry {
        int nodeId;
        int depth;
        VisibleEntry() : nodeId(-1), depth(0) {}
        VisibleEntry(int id, int d) : nodeId(id), depth(d) {}
    };

    void syncColumns();
    void rebuildIndex();
    void rebuildReverseIndex();
    void appendVisibleSubtree(int nodeId, int depth);
    bool nodeMatchesFilter(int nodeId) const;
    bool nodeVisibleInFilter(int nodeId) const;
    int contentLeft(int displayRow, int col, bool hasChildren) const;
    void drawExpandToggle(TQPainter* p, int x, int y, int h, bool expanded) const;
    bool hitExpandToggle(int displayRow, int x) const;
    void invalidateSortCache();
    void updateSearch();

    TQtAbstractItemModel* m_model;

    TQValueVector<VisibleEntry> m_visibleRows;
    TQMap<int, int> m_nodeToDisplay;

    TQMap<int, bool> m_expanded;

    bool m_sortingEnabled;
    int  m_sortColumn;
    bool m_sortAscending;

    TQString m_filterText;
    int m_filterColumn;
    TQString m_filterColumnValue;

    TQMap<long, TQPixmap> m_scaledIconCache;
    int m_blockPaintingDepth;

    TQString m_searchText;
    TQFrame* m_searchWidget;
    TQLabel* m_searchIconLabel;
    TQLabel* m_searchTextLabel;
    TQTimer* m_searchTimer;
};

#endif // TQTMVCTREEVIEW_H
