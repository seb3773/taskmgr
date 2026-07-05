#include "tqtabstractitemmodel.h"

TQtModelIndex TQtAbstractItemModel::parent(const TQtModelIndex& /*index*/) const
{
    return TQtModelIndex();
}

int TQtAbstractItemModel::childCount(const TQtModelIndex& /*parent*/) const
{
    return 0;
}

TQtModelIndex TQtAbstractItemModel::index(int /*row*/, int /*column*/,
                                          const TQtModelIndex& /*parent*/) const
{
    return TQtModelIndex();
}

int TQtAbstractItemModel::row(const TQtModelIndex& /*index*/) const
{
    return -1;
}

bool TQtAbstractItemModel::hasChildren(const TQtModelIndex& /*index*/) const
{
    return false;
}

#include "tqtabstractitemmodel.moc"
