#ifndef TQTLISTSTORE_H
#define TQTLISTSTORE_H

#include "tqtabstractlistmodel.h"
#include <ntqvaluevector.h>
#include <ntqmap.h>
#include <ntqstring.h>
#include <ntqvariant.h>
#include "tqtcellstyle.h"

typedef TQValueVector<TQVariant> TQtRow;

/**
 * @brief A conditional formatting rule.
 *
 * Supports two matching modes:
 * - **String**: cell text contains the match string (case-insensitive)
 * - **Numeric**: cell value compared against a threshold (>, <, ==, >=, <=, !=)
 *
 * Rules are evaluated lazily at paint time, only for visible cells.
 *
 * @code
 * // String rule: highlight cells containing "Error"
 * model->addStyleRule(TQtStyleRule("Error", -1,
 *     TQtCellStyle().setForeground(TQt::red).setBold(true)));
 *
 * // Numeric rule: highlight values > 1000 in column 1
 * model->addStyleRule(TQtStyleRule(TQtStyleRule::GT, 1000.0, 1,
 *     TQtCellStyle().setForeground(TQt::red)));
 *
 * // Numeric rule: highlight values == 0
 * model->addStyleRule(TQtStyleRule(TQtStyleRule::EQ, 0.0, -1,
 *     TQtCellStyle().setBackground(TQt::gray)));
 * @endcode
 */
struct TQtStyleRule
{
    enum MatchMode {
        Contains,  ///< String: cell text contains matchText (case-insensitive)
        GT,        ///< Numeric: cell value > numThreshold
        LT,        ///< Numeric: cell value < numThreshold
        EQ,        ///< Numeric: cell value == numThreshold
        GTE,       ///< Numeric: cell value >= numThreshold
        LTE,       ///< Numeric: cell value <= numThreshold
        NEQ        ///< Numeric: cell value != numThreshold
    };

    MatchMode   mode;          ///< Matching mode
    TQString     matchText;     ///< Text to search for (Contains mode, pre-lowered)
    double      numThreshold;  ///< Numeric threshold (GT/LT/EQ/GTE/LTE/NEQ modes)
    int         column;        ///< Column to match in (-1 = all columns)
    TQtCellStyle style;         ///< Style to apply when matched

    TQtStyleRule() : mode(Contains), numThreshold(0.0), column(-1) {}

    /// String rule constructor (backward compatible)
    TQtStyleRule(const TQString& text, int col, const TQtCellStyle& s)
        : mode(Contains), matchText(text.lower()), numThreshold(0.0),
          column(col), style(s) {}

    /// Numeric rule constructor
    TQtStyleRule(MatchMode m, double threshold, int col, const TQtCellStyle& s)
        : mode(m), numThreshold(threshold), column(col), style(s) {}
};

/**
 * @brief A concrete, GTK ListStore-style implementation of TQtAbstractListModel.
 *
 * Stores data in a flat list of rows. Provides simple, fast methods for
 * insertion, deletion, and modification, automatically emitting the correct
 * MVC signals to keep any connected views in sync.
 *
 * Usage example:
 * @code
 * TQtListStore* model = new TQtListStore(3); // 3 columns
 * model->setHeader(0, "Name");
 * model->setHeader(1, "Status");
 * model->setHeader(2, "Size");
 *
 * TQtRow row(3);
 * row[0] = TQVariant("hello.txt");
 * row[1] = TQVariant("OK");
 * row[2] = TQVariant(4096);
 * model->appendRow(row);
 * @endcode
 */
class TQtListStore : public TQtAbstractListModel
{
    TQ_OBJECT

public:
    /**
     * @brief Constructs a list store with a fixed number of columns.
     * @param columnCount Number of columns.
     */
    TQtListStore(int columnCount, TQObject* parent = 0);
    ~TQtListStore();

    // -- TQtAbstractListModel interface --
    int rowCount() const;
    int columnCount() const;
    TQVariant data(int row, int column) const;
    TQString headerData(int column) const;

    // -- Data manipulation --

    /**
     * @brief Sets the header label for a column.
     */
    void setHeader(int column, const TQString& label);

    /**
     * @brief Appends a new row at the end of the list.
     * @param rowData A vector of TQVariant values, one per column.
     */
    void appendRow(const TQtRow& rowData);

    /**
     * @brief Inserts a new row before the given row index.
     */
    void insertRow(int row, const TQtRow& rowData);

    /**
     * @brief Removes the row at the given index.
     */
    void removeRow(int row);

    /**
     * @brief Removes 'count' rows starting at 'startRow'.
     * More efficient than calling removeRow() in a loop.
     */
    void removeRows(int startRow, int count);

    /**
     * @brief Updates a single cell's value.
     */
    void setData(int row, int column, const TQVariant& value);

    /**
     * @brief Removes all rows from the model.
     */
    void clear();

    /**
     * @brief Returns the effective visual style for a cell.
     * The cell style (if any) overrides the column style (if any).
     */
    virtual TQtCellStyle cellStyle(int row, int column) const;

    /**
     * @brief Overload using pre-fetched cell text (avoids double data() call).
     */
    virtual TQtCellStyle cellStyle(int row, int column, const TQString& cellText) const;

    /**
     * @brief Sets a default style for an entire column.
     * Individual cells can override this with setCellStyle().
     */
    void setColumnStyle(int column, const TQtCellStyle& style);

    /**
     * @brief Sets the style for a specific cell, overriding the column style.
     */
    void setCellStyle(int row, int column, const TQtCellStyle& style);

    /**
     * @brief Clears the style for a specific cell (falls back to column style).
     */
    void clearCellStyle(int row, int column);

    /**
     * @brief Clears the style for a column.
     */
    void clearColumnStyle(int column);

    /**
     * @brief Adds a conditional formatting rule.
     * When a cell's text contains matchText (case-insensitive),
     * the rule's style is applied. Set column to -1 to match all columns.
     * Rules are evaluated lazily at paint time (O(1) memory).
     * Multiple rules can stack; later rules override earlier ones.
     */
    void addStyleRule(const TQtStyleRule& rule);

    /**
     * @brief Removes all style rules.
     */
    void clearStyleRules();

    /**
     * @brief Starts a batch operation. Signals are suppressed until endBatch().
     * Use this for bulk insertions to avoid costly per-row repaints.
     */
    void beginBatch();

    /**
     * @brief Ends a batch operation and emits a single modelReset() signal.
     */
    void endBatch();

private:
    int m_columnCount;
    int m_batchDepth;  // 0 = not in batch, >0 = nested batch depth
    TQValueVector<TQtRow> m_rows;
    TQValueVector<TQString> m_headers;

    // Column-level styles (key = column index)
    TQMap<int, TQtCellStyle> m_columnStyles;

    // Cell-level styles (struct key avoids TQString overhead of "row:col" keys)
    struct CellCoord {
        int row;
        int col;
        CellCoord() : row(0), col(0) {}
        CellCoord(int r, int c) : row(r), col(c) {}
        bool operator<(const CellCoord& o) const {
            return row != o.row ? row < o.row : col < o.col;
        }
    };
    TQMap<CellCoord, TQtCellStyle> m_cellStyles;

    TQValueVector<TQtStyleRule> m_styleRules;
};

#endif // TQTLISTSTORE_H
