#ifndef TQTMVCTABLEVIEW_H
#define TQTMVCTABLEVIEW_H

#include <ntqtable.h>
#include <ntqvaluevector.h>
#include <ntqpoint.h>
#include "tqtabstractlistmodel.h"

/**
 * @brief A high-performance virtual table view driven by a TQtAbstractListModel.
 *
 * Features:
 * - Virtual cell rendering (O(1) memory, only visible cells drawn)
 * - Column sorting (click header to toggle asc/desc)
 * - Live text filtering across all columns
 * - Style support via TQtCellStyle
 */
class TQtMvcTableView : public TQTable
{
    TQ_OBJECT

public:
    TQtMvcTableView(TQWidget* parent = 0);
    ~TQtMvcTableView();

    void blockPainting() { m_blockPaintingDepth++; }
    void unblockPainting() {
        if (m_blockPaintingDepth > 0) {
            m_blockPaintingDepth--;
            if (m_blockPaintingDepth == 0 && viewport()) {
                viewport()->update();
            }
        }
    }

    /**
     * @brief Connects the view to the given model.
     */
    void setModel(TQtAbstractListModel* model);

    /**
     * @brief Returns the currently set model.
     */
    TQtAbstractListModel* model() const;

    /**
     * @brief Enables or disables column header sorting. Enabled by default.
     */
    void setSortingEnabled(bool enabled);

    /**
     * @brief Sorts the view by the given column.
     * @param ascending true = A→Z / 0→9, false = Z→A / 9→0
     */
    void sortByColumn(int column, bool ascending = true);

    int sortColumn() const { return m_sortColumn; }
    bool sortAscending() const { return m_sortAscending; }

    /**
     * @brief Sets a text filter. Only rows containing this text (case-insensitive,
     * in any column) are displayed. Pass an empty string to clear.
     */
    void setFilterText(const TQString& text);

    /**
     * @brief Sets an exact-match filter on a specific column.
     * @param column The column to filter, or -1 to disable ("All").
     * @param matchValue The exact string to match.
     * 
     * This filter is combined (AND) with the global text filter (setFilterText).
     */
    void setColumnFilter(int column, const TQString& matchValue);

    /**
     * @brief Returns the model row index for a given display row.
     * Useful for mapping selections back to the model.
     */
    int modelRow(int displayRow) const;

    /**
     * @brief Returns the currently selected model row, or -1 if none.
     */
    int selectedModelRow() const;

    /**
     * @brief Returns the display row for a given model row, or -1 if not visible.
     */
    int displayRowForModel(int mRow) const;

    /**
     * @brief Selects the row corresponding to the given model row index.
     */
    void selectModelRow(int mRow);

    /**
     * @brief Scrolls the view so that the given model row is visible.
     */
    void ensureModelRowVisible(int modelRow);

signals:
    /**
     * @brief Emitted when the selected row changes.
     * @param modelRow The model row index of the new selection, or -1 if none.
     */
    void selectionChanged(int modelRow);

    /**
     * @brief Emitted when the user right-clicks on the view.
     * @param modelRow The model row at the click position, or -1.
     * @param column   The column at the click position.
     * @param globalPos The global screen position for popup menus.
     */
    void rowContextMenuRequested(int modelRow, int column, const TQPoint& globalPos);

protected:
    void paintCell(TQPainter* p, int row, int col, const TQRect& cr, bool selected);
    void paintEmptyArea(TQPainter* p, int cx, int cy, int cw, int ch);
    void viewportPaintEvent(TQPaintEvent* e);
    TQTableItem* item(int row, int col) const { return 0; }
    void currentChanged(int row, int col);
    void contentsContextMenuEvent(TQContextMenuEvent* e);
    bool eventFilter(TQObject* watched, TQEvent* event);

private slots:
    void onDataChanged(int rowStart, int rowEnd);
    void onRowsInserted(int row, int count);
    void onRowsRemoved(int row, int count);
    void onModelReset();
    void onHeaderClicked(int col);

private:
    void syncColumns();
    void rebuildIndex();

    TQtAbstractListModel* m_model;

    // Mapping from display-row to model-row
    TQValueVector<int> m_visibleRows;
    // Reverse mapping: model-row → display-row (-1 = not visible). O(1) lookup.
    TQValueVector<int> m_reverseIndex;
    void rebuildReverseIndex();

    // Sort state
    bool m_sortingEnabled;
    int  m_sortColumn;    // -1 = no sort
    bool m_sortAscending;

    // Sort key cache — reused when toggling asc/desc on same column
    int  m_sortCacheColumn;     // column cached for, -1 = invalid
    bool m_sortCacheAllNumeric;
    TQValueVector<double> m_sortCacheNum;      // indexed by model row
    TQValueVector<TQString> m_sortCacheStr;    // indexed by model row (mixed only)
    TQValueVector<bool>   m_sortCacheIsNum;    // indexed by model row (mixed only)
    void invalidateSortCache();

    // Filter state
    TQString m_filterText;
    int m_filterColumn;
    TQString m_filterColumnValue;

    // Scaled icon cache — avoids smoothScale() at every repaint
    // Key: (pixmap serialNumber << 16) | targetHeight
    TQMap<long, TQPixmap> m_scaledIconCache;
    int m_blockPaintingDepth;
};

#endif // TQTMVCTABLEVIEW_H
