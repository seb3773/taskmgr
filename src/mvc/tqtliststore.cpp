#include "tqtliststore.h"
#include "tqtcommon_p.h"

TQtListStore::TQtListStore(int columnCount, TQObject* parent)
    : TQtAbstractListModel(parent),
      m_columnCount(columnCount),
      m_batchDepth(0)
{
    m_headers.resize(columnCount);
    for (int i = 0; i < columnCount; ++i)
        m_headers[i] = TQString::number(i + 1);
}

TQtListStore::~TQtListStore()
{
}

int TQtListStore::rowCount() const
{
    return (int)m_rows.size();
}

int TQtListStore::columnCount() const
{
    return m_columnCount;
}

TQVariant TQtListStore::data(int row, int column) const
{
    if (row < 0 || row >= (int)m_rows.size()) return TQVariant();
    if (column < 0 || column >= m_columnCount) return TQVariant();
    const TQtRow& r = m_rows[row];
    if (column >= (int)r.size()) return TQVariant();
    return r[column];
}

TQString TQtListStore::headerData(int column) const
{
    if (column < 0 || column >= (int)m_headers.size()) return TQString();
    return m_headers[column];
}

void TQtListStore::setHeader(int column, const TQString& label)
{
    if (column < 0 || column >= m_columnCount) return;
    m_headers[column] = label;
}

void TQtListStore::appendRow(const TQtRow& rowData)
{
    int newRow = (int)m_rows.size();
    m_rows.push_back(rowData);
    if (m_batchDepth == 0) emit rowsInserted(newRow, 1);
}

void TQtListStore::insertRow(int row, const TQtRow& rowData)
{
    if (row < 0) row = 0;
    if (row > (int)m_rows.size()) row = (int)m_rows.size();
    m_rows.insert(m_rows.begin() + row, rowData);
    if (m_batchDepth == 0) emit rowsInserted(row, 1);
}

void TQtListStore::removeRow(int row)
{
    if (row < 0 || row >= (int)m_rows.size()) return;
    m_rows.erase(m_rows.begin() + row);
    if (m_batchDepth == 0) emit rowsRemoved(row, 1);
}

void TQtListStore::removeRows(int startRow, int count)
{
    if (startRow < 0 || count <= 0) return;
    int end = startRow + count;
    if (end > (int)m_rows.size()) end = (int)m_rows.size();
    count = end - startRow;
    if (count <= 0) return;
    m_rows.erase(m_rows.begin() + startRow, m_rows.begin() + end);
    if (m_batchDepth == 0) emit rowsRemoved(startRow, count);
}

void TQtListStore::setData(int row, int column, const TQVariant& value)
{
    if (row < 0 || row >= (int)m_rows.size()) return;
    if (column < 0 || column >= m_columnCount) return;
    if (column >= (int)m_rows[row].size())
        m_rows[row].resize(column + 1);
    m_rows[row][column] = value;
    if (m_batchDepth == 0) emit dataChanged(row, row);
}

void TQtListStore::clear()
{
    m_rows.clear();
    if (m_batchDepth == 0) emit modelReset();
}

// ---- Style API ----

TQtCellStyle TQtListStore::cellStyle(int row, int column) const
{
    // Delegate to the cached-text overload
    return cellStyle(row, column, data(row, column).toString());
}

TQtCellStyle TQtListStore::cellStyle(int row, int column, const TQString& cellText) const
{
    // Layer 1: column style (base)
    TQtCellStyle result;
    {
        TQMap<int, TQtCellStyle>::ConstIterator it = m_columnStyles.find(column);
        if (it != m_columnStyles.end()) result = it.data();
    }

    // Layer 2: style rules (conditional — text already provided, zero allocation)
    // Lazy numeric conversion: toDouble() called at most once per cell
    bool numConverted = false;
    bool numValid = false;
    double numVal = 0.0;

    for (int i = 0; i < (int)m_styleRules.size(); ++i) {
        const TQtStyleRule& rule = m_styleRules[i];
        if (rule.column >= 0 && rule.column != column) continue;

        bool matched = false;
        if (rule.mode == TQtStyleRule::Contains) {
            matched = containsCI(cellText, rule.matchText);
        } else {
            // Convert once, reuse for all numeric rules on this cell
            if (!numConverted) {
                numVal = cellText.toDouble(&numValid);
                numConverted = true;
            }
            if (numValid) {
                switch (rule.mode) {
                    case TQtStyleRule::GT:  matched = (numVal >  rule.numThreshold); break;
                    case TQtStyleRule::LT:  matched = (numVal <  rule.numThreshold); break;
                    case TQtStyleRule::EQ:  matched = (numVal == rule.numThreshold); break;
                    case TQtStyleRule::GTE: matched = (numVal >= rule.numThreshold); break;
                    case TQtStyleRule::LTE: matched = (numVal <= rule.numThreshold); break;
                    case TQtStyleRule::NEQ: matched = (numVal != rule.numThreshold); break;
                    default: break;
                }
            }
        }
        if (matched)
            result = result.merged(rule.style);
    }

    // Layer 3: explicit cell style (highest priority)
    {
        CellCoord key(row, column);
        TQMap<CellCoord, TQtCellStyle>::ConstIterator it = m_cellStyles.find(key);
        if (it != m_cellStyles.end())
            result = result.merged(it.data());
    }

    return result;
}

void TQtListStore::setColumnStyle(int column, const TQtCellStyle& style)
{
    m_columnStyles[column] = style;
    if (m_batchDepth == 0 && rowCount() > 0)
        emit dataChanged(0, rowCount() - 1);
}

void TQtListStore::setCellStyle(int row, int column, const TQtCellStyle& style)
{
    m_cellStyles[CellCoord(row, column)] = style;
    if (m_batchDepth == 0)
        emit dataChanged(row, row);
}

void TQtListStore::clearCellStyle(int row, int column)
{
    m_cellStyles.remove(CellCoord(row, column));
    if (m_batchDepth == 0)
        emit dataChanged(row, row);
}

void TQtListStore::clearColumnStyle(int column)
{
    m_columnStyles.remove(column);
    if (m_batchDepth == 0 && rowCount() > 0)
        emit dataChanged(0, rowCount() - 1);
}

void TQtListStore::beginBatch()
{
    ++m_batchDepth;
}

void TQtListStore::endBatch()
{
    if (m_batchDepth > 0) --m_batchDepth;
    if (m_batchDepth == 0) emit modelReset();
}

void TQtListStore::addStyleRule(const TQtStyleRule& rule)
{
    m_styleRules.push_back(rule);
    if (m_batchDepth == 0 && rowCount() > 0)
        emit dataChanged(0, rowCount() - 1);
}

void TQtListStore::clearStyleRules()
{
    m_styleRules.clear();
    if (m_batchDepth == 0 && rowCount() > 0)
        emit dataChanged(0, rowCount() - 1);
}

#include "tqtliststore.moc"
