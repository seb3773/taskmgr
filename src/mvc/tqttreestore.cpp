#include "tqttreestore.h"
#include "tqtcommon_p.h"
#include <algorithm>

TQtTreeStore::TQtTreeStore(int columnCount, TQObject* parent)
    : TQtAbstractItemModel(parent),
      m_columnCount(columnCount),
      m_batchDepth(0)
{
    m_headers.resize(columnCount);
    for (int i = 0; i < columnCount; ++i)
        m_headers[i] = TQString::number(i + 1);
}

TQtTreeStore::~TQtTreeStore()
{
}

bool TQtTreeStore::nodeValid(int nodeId) const
{
    return nodeId >= 0 && nodeId < (int)m_nodes.size() && m_nodes[nodeId].valid;
}

const TQValueVector<int>* TQtTreeStore::childrenList(int parentNodeId) const
{
    if (parentNodeId == RootNodeId) return &m_rootChildren;
    if (!nodeValid(parentNodeId)) return 0;
    return &m_nodes[parentNodeId].children;
}

TQValueVector<int>* TQtTreeStore::childrenList(int parentNodeId)
{
    if (parentNodeId == RootNodeId) return &m_rootChildren;
    if (!nodeValid(parentNodeId)) return 0;
    return &m_nodes[parentNodeId].children;
}

int TQtTreeStore::validChildCount(const TQValueVector<int>& children) const
{
    int count = 0;
    for (int i = 0; i < (int)children.size(); ++i) {
        if (nodeValid(children[i])) ++count;
    }
    return count;
}

int TQtTreeStore::validChildAt(const TQValueVector<int>& children, int index) const
{
    int seen = 0;
    for (int i = 0; i < (int)children.size(); ++i) {
        if (!nodeValid(children[i])) continue;
        if (seen == index) return children[i];
        ++seen;
    }
    return RootNodeId;
}

int TQtTreeStore::columnCount() const
{
    return m_columnCount;
}

TQtModelIndex TQtTreeStore::parent(const TQtModelIndex& index) const
{
    if (!index.isValid() || !nodeValid(index.nodeId)) return TQtModelIndex();
    int pid = m_nodes[index.nodeId].parentId;
    if (pid < 0) return TQtModelIndex();
    return TQtModelIndex(pid, 0);
}

int TQtTreeStore::childCount(const TQtModelIndex& parent) const
{
    const TQValueVector<int>* children = parent.isValid()
        ? childrenList(parent.nodeId)
        : &m_rootChildren;
    if (!children) return 0;
    return validChildCount(*children);
}

TQtModelIndex TQtTreeStore::index(int row, int column, const TQtModelIndex& parent) const
{
    const TQValueVector<int>* children = parent.isValid()
        ? childrenList(parent.nodeId)
        : &m_rootChildren;
    if (!children || row < 0 || column < 0 || column >= m_columnCount) return TQtModelIndex();
    int nodeId = validChildAt(*children, row);
    if (nodeId < 0) return TQtModelIndex();
    return TQtModelIndex(nodeId, column);
}

int TQtTreeStore::row(const TQtModelIndex& index) const
{
    if (!index.isValid()) return -1;
    return rowAmongSiblings(index.nodeId);
}

int TQtTreeStore::rowAmongSiblings(int nodeId) const
{
    if (!nodeValid(nodeId)) return -1;
    int pid = m_nodes[nodeId].parentId;
    const TQValueVector<int>* children = childrenList(pid);
    if (!children) return -1;
    int row = 0;
    for (int i = 0; i < (int)children->size(); ++i) {
        int cid = (*children)[i];
        if (!nodeValid(cid)) continue;
        if (cid == nodeId) return row;
        ++row;
    }
    return -1;
}

bool TQtTreeStore::hasChildren(const TQtModelIndex& index) const
{
    if (!index.isValid()) return false;
    return childCount(index) > 0;
}

TQVariant TQtTreeStore::data(const TQtModelIndex& index) const
{
    if (!index.isValid() || !nodeValid(index.nodeId)) return TQVariant();
    if (index.column < 0 || index.column >= m_columnCount) return TQVariant();
    const TQtRow& r = m_nodes[index.nodeId].data;
    if (index.column >= (int)r.size()) return TQVariant();
    return r[index.column];
}

TQString TQtTreeStore::headerData(int column) const
{
    if (column < 0 || column >= (int)m_headers.size()) return TQString();
    return m_headers[column];
}

TQtCellStyle TQtTreeStore::cellStyle(const TQtModelIndex& index) const
{
    if (!index.isValid()) return TQtCellStyle();
    return cellStyleForNode(index.nodeId, index.column, data(index).toString());
}

TQtCellStyle TQtTreeStore::cellStyle(const TQtModelIndex& index, const TQString& cellText) const
{
    if (!index.isValid()) return TQtCellStyle();
    return cellStyleForNode(index.nodeId, index.column, cellText);
}

TQtCellStyle TQtTreeStore::cellStyleForNode(int nodeId, int column, const TQString& cellText) const
{
    /* 1. Per-cell style has highest priority — set via setCellStyle() */
    {
        CellCoord key(nodeId, column);
        TQMap<CellCoord, TQtCellStyle>::ConstIterator it = m_cellStyles.find(key);
        if (it != m_cellStyles.end()) {
            return it.data();
        }
    }

    TQtCellStyle result;
    {
        TQMap<int, TQtCellStyle>::ConstIterator it = m_columnStyles.find(column);
        if (it != m_columnStyles.end()) result = it.data();
    }

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

        if (matched) {
            if (rule.style.hasBackground) {
                result.background = rule.style.background;
                result.hasBackground = true;
            }
            if (rule.style.hasForeground) {
                result.foreground = rule.style.foreground;
                result.hasForeground = true;
            }
            if (rule.style.hasBold) {
                result.bold = rule.style.bold;
                result.hasBold = true;
            }
            if (rule.style.hasItalic) {
                result.italic = rule.style.italic;
                result.hasItalic = true;
            }
            if (rule.style.hasUnderline) {
                result.underline = rule.style.underline;
                result.hasUnderline = true;
            }
        }
    }

    return result;
}

void TQtTreeStore::setHeader(int column, const TQString& label)
{
    if (column >= 0 && column < (int)m_headers.size()) {
        m_headers[column] = label;
        if (m_batchDepth == 0)
            emit layoutChanged();
    }
}

int TQtTreeStore::appendNode(int parentNodeId, const TQtRow& rowData)
{
    if (parentNodeId != RootNodeId && !nodeValid(parentNodeId)) return RootNodeId;

    int nodeId = (int)m_nodes.size();
    TreeNode node;
    node.data = rowData;
    node.parentId = parentNodeId;
    node.valid = true;
    m_nodes.push_back(node);

    TQValueVector<int>* children = childrenList(parentNodeId);
    if (!children) {
        m_nodes[nodeId].valid = false;
        return RootNodeId;
    }

    int insertRow = validChildCount(*children);
    children->push_back(nodeId);

    if (m_batchDepth == 0) {
        TQtModelIndex parentIdx = (parentNodeId >= 0)
            ? TQtModelIndex(parentNodeId, 0) : TQtModelIndex();
        emit indexRowsInserted(parentIdx, insertRow, 1);
    }

    return nodeId;
}

void TQtTreeStore::removeNodeInternal(int nodeId)
{
    if (!nodeValid(nodeId)) return;

    const TQValueVector<int> childCopy = m_nodes[nodeId].children;
    for (int i = 0; i < (int)childCopy.size(); ++i)
        removeNodeInternal(childCopy[i]);

    int pid = m_nodes[nodeId].parentId;
    int siblingRow = rowAmongSiblings(nodeId);

    TQValueVector<int>* siblings = childrenList(pid);
    if (siblings) {
        for (int i = 0; i < (int)siblings->size(); ++i) {
            if ((*siblings)[i] == nodeId) {
                siblings->erase(siblings->begin() + i);
                break;
            }
        }
    }

    m_nodes[nodeId].valid = false;
    m_nodes[nodeId].children.clear();

    if (m_batchDepth == 0 && siblingRow >= 0) {
        TQtModelIndex parentIdx = (pid >= 0) ? TQtModelIndex(pid, 0) : TQtModelIndex();
        emit indexRowsRemoved(parentIdx, siblingRow, 1);
    }
}

void TQtTreeStore::removeNode(int nodeId)
{
    removeNodeInternal(nodeId);
}

void TQtTreeStore::setData(int nodeId, int column, const TQVariant& value)
{
    if (!nodeValid(nodeId)) return;
    if (column < 0 || column >= m_columnCount) return;
    if (column >= (int)m_nodes[nodeId].data.size())
        m_nodes[nodeId].data.resize(column + 1);
    m_nodes[nodeId].data[column] = value;

    if (m_batchDepth == 0) {
        TQtModelIndex idx(nodeId, column);
        emit indexDataChanged(idx, idx);
    }
}

int TQtTreeStore::childCount(int nodeId) const
{
    const TQValueVector<int>* children = childrenList(nodeId);
    if (!children) return 0;
    return validChildCount(*children);
}

int TQtTreeStore::parentId(int nodeId) const
{
    if (!nodeValid(nodeId)) return RootNodeId;
    return m_nodes[nodeId].parentId;
}

int TQtTreeStore::childAt(int parentNodeId, int index) const
{
    const TQValueVector<int>* children = childrenList(parentNodeId);
    if (!children) return RootNodeId;
    return validChildAt(*children, index);
}

static double parseNumeric(const TQString& str, bool* ok) {
    TQString clean = str.stripWhiteSpace();
    if (clean.isEmpty()) {
        *ok = false;
        return 0.0;
    }
    
    double multiplier = 1.0;
    if (clean.endsWith("TB") || clean.endsWith("To")) {
        multiplier = 1024.0 * 1024.0 * 1024.0 * 1024.0;
        clean.truncate(clean.length() - 2);
    } else if (clean.endsWith("GB") || clean.endsWith("Go")) {
        multiplier = 1024.0 * 1024.0 * 1024.0;
        clean.truncate(clean.length() - 2);
    } else if (clean.endsWith("MB") || clean.endsWith("Mo")) {
        multiplier = 1024.0 * 1024.0;
        clean.truncate(clean.length() - 2);
    } else if (clean.endsWith("KB") || clean.endsWith("Ko")) {
        multiplier = 1024.0;
        clean.truncate(clean.length() - 2);
    } else if (clean.endsWith("B") || clean.endsWith("o")) {
        multiplier = 1.0;
        clean.truncate(clean.length() - 1);
    } else if (clean.endsWith("%")) {
        clean.truncate(clean.length() - 1);
    } else if (clean.endsWith("GHz")) {
        multiplier = 1000.0 * 1000.0 * 1000.0;
        clean.truncate(clean.length() - 3);
    } else if (clean.endsWith("MHz")) {
        multiplier = 1000.0 * 1000.0;
        clean.truncate(clean.length() - 3);
    }
    
    clean = clean.stripWhiteSpace();
    clean.replace(',', ".");
    
    double val = clean.toDouble(ok);
    if (*ok) {
        return val * multiplier;
    }
    return 0.0;
}

struct SortEntry {
    int nodeId;
    double numVal;
    bool isNum;
    TQString strVal;
};

struct SortEntryComparator {
    bool ascending;
    SortEntryComparator(bool asc) : ascending(asc) {}
    bool operator()(const SortEntry& a, const SortEntry& b) const {
        if (a.isNum && b.isNum)
            return ascending ? a.numVal < b.numVal : a.numVal > b.numVal;
        int cmp = TQString::localeAwareCompare(a.strVal, b.strVal);
        return ascending ? cmp < 0 : cmp > 0;
    }
};

void TQtTreeStore::sortChildrenInternal(int parentNodeId, int column, bool ascending)
{
    TQValueVector<int>* children = childrenList(parentNodeId);
    if (!children || children->isEmpty()) return;

    int n = children->size();
    TQValueVector<SortEntry> entries;
    entries.resize(n);
    for (int i = 0; i < n; ++i) {
        int nid = (*children)[i];
        entries[i].nodeId = nid;
        entries[i].strVal = data(TQtModelIndex(nid, column)).toString();
        entries[i].numVal = parseNumeric(entries[i].strVal, &entries[i].isNum);
    }

    std::stable_sort(entries.begin(), entries.end(), SortEntryComparator(ascending));

    for (int i = 0; i < n; ++i) {
        (*children)[i] = entries[i].nodeId;
    }
}

void TQtTreeStore::sortChildren(int parentNodeId, int column, bool ascending)
{
    sortChildrenInternal(parentNodeId, column, ascending);
    if (m_batchDepth == 0)
        emit layoutChanged();
}

void TQtTreeStore::sortAllChildrenRecursive(int parentNodeId, int column, bool ascending)
{
    sortChildrenInternal(parentNodeId, column, ascending);

    const TQValueVector<int>* children = childrenList(parentNodeId);
    if (children) {
        for (int i = 0; i < (int)children->size(); ++i) {
            int childId = (*children)[i];
            if (nodeValid(childId) && childCount(childId) > 0)
                sortAllChildrenRecursive(childId, column, ascending);
        }
    }
}

void TQtTreeStore::sortAllChildren(int column, bool ascending)
{
    sortAllChildrenRecursive(RootNodeId, column, ascending);
    if (m_batchDepth == 0)
        emit layoutChanged();
}

void TQtTreeStore::clear()
{
    m_nodes.clear();
    m_rootChildren.clear();
    m_cellStyles.clear();
    if (m_batchDepth == 0)
        emit modelReset();
}

void TQtTreeStore::setColumnStyle(int column, const TQtCellStyle& style)
{
    m_columnStyles[column] = style;
    if (m_batchDepth == 0)
        emit layoutChanged();
}

void TQtTreeStore::setCellStyle(int nodeId, int column, const TQtCellStyle& style)
{
    m_cellStyles[CellCoord(nodeId, column)] = style;
    if (m_batchDepth == 0) {
        TQtModelIndex idx(nodeId, column);
        emit indexDataChanged(idx, idx);
    }
}

void TQtTreeStore::clearCellStyle(int nodeId, int column)
{
    m_cellStyles.remove(CellCoord(nodeId, column));
    if (m_batchDepth == 0) {
        TQtModelIndex idx(nodeId, column);
        emit indexDataChanged(idx, idx);
    }
}

void TQtTreeStore::clearColumnStyle(int column)
{
    m_columnStyles.remove(column);
    if (m_batchDepth == 0)
        emit layoutChanged();
}

void TQtTreeStore::addStyleRule(const TQtStyleRule& rule)
{
    m_styleRules.push_back(rule);
    if (m_batchDepth == 0)
        emit layoutChanged();
}

void TQtTreeStore::clearStyleRules()
{
    m_styleRules.clear();
    if (m_batchDepth == 0)
        emit layoutChanged();
}

void TQtTreeStore::beginBatch()
{
    ++m_batchDepth;
}

void TQtTreeStore::endBatch()
{
    if (m_batchDepth > 0) --m_batchDepth;
    if (m_batchDepth == 0)
        emit modelReset();
}

#include "tqttreestore.moc"
