#ifndef TQTABSTRACTITEMMODEL_H
#define TQTABSTRACTITEMMODEL_H

#include <ntqobject.h>
#include <ntqvariant.h>
#include <ntqstring.h>
#include <ntqpixmap.h>
#include "tqtmodelindex.h"
#include "tqtcellstyle.h"

/**
 * @brief Abstract base class for flat and tree MVC models.
 *
 * Tree models implement the full hierarchy API. Flat list models (TQtAbstractListModel)
 * provide default implementations that map nodeId to row index.
 */
class TQtAbstractItemModel : public TQObject
{
    TQ_OBJECT

public:
    TQtAbstractItemModel(TQObject* parent = 0) : TQObject(parent) {}
    virtual ~TQtAbstractItemModel() {}

    virtual int columnCount() const = 0;

    /** @brief Returns the parent of index, or an invalid index for top-level nodes. */
    virtual TQtModelIndex parent(const TQtModelIndex& index) const;

    /** @brief Number of children of parent. Invalid parent = top-level nodes. */
    virtual int childCount(const TQtModelIndex& parent) const;

    /** @brief Returns the index of the child at row under parent. */
    virtual TQtModelIndex index(int row, int column, const TQtModelIndex& parent) const;

    /** @brief Returns the row of index among its siblings, or -1 if invalid. */
    virtual int row(const TQtModelIndex& index) const;

    /** @brief Returns true if the node has at least one child. */
    virtual bool hasChildren(const TQtModelIndex& index) const;

    /** @brief Returns cell data for the given index. */
    virtual TQVariant data(const TQtModelIndex& index) const = 0;

    virtual TQString headerData(int column) const {
        return TQString::number(column + 1);
    }

    virtual TQPixmap decoration(const TQtModelIndex& /*index*/) const {
        return TQPixmap();
    }

    virtual TQtCellStyle cellStyle(const TQtModelIndex& /*index*/) const {
        return TQtCellStyle();
    }

    virtual TQtCellStyle cellStyle(const TQtModelIndex& index, const TQString& /*cellText*/) const {
        return cellStyle(index);
    }

signals:
    void indexDataChanged(const TQtModelIndex& topLeft, const TQtModelIndex& bottomRight);
    void indexRowsInserted(const TQtModelIndex& parent, int first, int count);
    void indexRowsRemoved(const TQtModelIndex& parent, int first, int count);
    void modelReset();
    void layoutChanged();
};

#endif // TQTABSTRACTITEMMODEL_H
