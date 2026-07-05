#ifndef TQTTREESTORE_H
#define TQTTREESTORE_H

#include "tqtabstractitemmodel.h"
#include "tqtliststore.h"
#include <ntqvaluevector.h>
#include <ntqmap.h>
#include "tqtcellstyle.h"

typedef TQValueVector<TQVariant> TQtRow;

/**
 * @brief A concrete, GTK TreeStore-style implementation of TQtAbstractItemModel.
 *
 * Nodes are identified by stable nodeId values that are never reused after removal.
 * Designed for task-manager style hierarchies (e.g. process groups → processes).
 */
class TQtTreeStore : public TQtAbstractItemModel
{
    TQ_OBJECT

public:
    /**
     * @brief Constructs a tree store with a fixed number of columns.
     */
    TQtTreeStore(int columnCount, TQObject* parent = 0);
    ~TQtTreeStore();

    static const int RootNodeId = -1;

    // -- TQtAbstractItemModel interface --
    int columnCount() const;
    TQtModelIndex parent(const TQtModelIndex& index) const;
    int childCount(const TQtModelIndex& parent) const;
    TQtModelIndex index(int row, int column, const TQtModelIndex& parent) const;
    int row(const TQtModelIndex& index) const;
    bool hasChildren(const TQtModelIndex& index) const;
    TQVariant data(const TQtModelIndex& index) const;
    TQString headerData(int column) const;
    TQtCellStyle cellStyle(const TQtModelIndex& index) const;
    TQtCellStyle cellStyle(const TQtModelIndex& index, const TQString& cellText) const;

    // -- Tree data manipulation --

    void setHeader(int column, const TQString& label);

    /**
     * @brief Appends a new node under parentNodeId (RootNodeId for top-level).
     * @return Stable nodeId of the new node, or -1 on failure.
     */
    int appendNode(int parentNodeId, const TQtRow& rowData);

    /**
     * @brief Removes a node and all its descendants.
     */
    void removeNode(int nodeId);

    /**
     * @brief Updates a single cell's value.
     */
    void setData(int nodeId, int column, const TQVariant& value);

    /**
     * @brief Returns the number of valid children of a node (RootNodeId = roots).
     */
    int childCount(int nodeId) const;

    /**
     * @brief Returns the parent nodeId, or RootNodeId for top-level nodes.
     */
    int parentId(int nodeId) const;

    /**
     * @brief Returns the nodeId of the child at index under parentNodeId.
     */
    int childAt(int parentNodeId, int index) const;

    /**
     * @brief Sorts the direct children of parentNodeId by column value.
     */
    void sortChildren(int parentNodeId, int column, bool ascending = true);

    /**
     * @brief Sorts children at every tree level (siblings only, hierarchy preserved).
     */
    void sortAllChildren(int column, bool ascending = true);

    /**
     * @brief Removes all nodes.
     */
    void clear();

    // -- Style API (same layering as TQtListStore, keyed by nodeId) --

    void setColumnStyle(int column, const TQtCellStyle& style);
    void setCellStyle(int nodeId, int column, const TQtCellStyle& style);
    void clearCellStyle(int nodeId, int column);
    void clearColumnStyle(int column);
    void addStyleRule(const TQtStyleRule& rule);
    void clearStyleRules();

    void beginBatch();
    void endBatch();

private:
    struct TreeNode {
        TQtRow data;
        int parentId;
        TQValueVector<int> children;
        bool valid;

        TreeNode() : parentId(RootNodeId), valid(false) {}
    };

    bool nodeValid(int nodeId) const;
    const TQValueVector<int>* childrenList(int parentNodeId) const;
    TQValueVector<int>* childrenList(int parentNodeId);
    int validChildCount(const TQValueVector<int>& children) const;
    int validChildAt(const TQValueVector<int>& children, int index) const;
    int rowAmongSiblings(int nodeId) const;
    void removeNodeInternal(int nodeId);
    void sortChildrenInternal(int parentNodeId, int column, bool ascending);
    void sortAllChildrenRecursive(int parentNodeId, int column, bool ascending);
    TQtCellStyle cellStyleForNode(int nodeId, int column, const TQString& cellText) const;

    int m_columnCount;
    int m_batchDepth;
    TQValueVector<TreeNode> m_nodes;
    TQValueVector<int> m_rootChildren;
    TQValueVector<TQString> m_headers;

    TQMap<int, TQtCellStyle> m_columnStyles;

    struct CellCoord {
        int nodeId;
        int col;
        CellCoord() : nodeId(0), col(0) {}
        CellCoord(int n, int c) : nodeId(n), col(c) {}
        bool operator<(const CellCoord& o) const {
            return nodeId != o.nodeId ? nodeId < o.nodeId : col < o.col;
        }
    };
    TQMap<CellCoord, TQtCellStyle> m_cellStyles;
    TQValueVector<TQtStyleRule> m_styleRules;
};

#endif // TQTTREESTORE_H
