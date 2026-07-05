#include "tqtabstractlistmodel.h"

TQString TQtAbstractListModel::headerData(int column) const
{
    return TQtAbstractItemModel::headerData(column);
}

TQPixmap TQtAbstractListModel::decoration(int /*row*/, int /*column*/) const
{
    return TQPixmap();
}

TQtCellStyle TQtAbstractListModel::cellStyle(int /*row*/, int /*column*/) const
{
    return TQtCellStyle();
}

TQtCellStyle TQtAbstractListModel::cellStyle(int row, int column, const TQString& /*cellText*/) const
{
    return cellStyle(row, column);
}

int TQtAbstractListModel::childCount(const TQtModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return rowCount();
}

TQtModelIndex TQtAbstractListModel::index(int row, int column, const TQtModelIndex& parent) const
{
    if (parent.isValid() || row < 0 || row >= rowCount()) return TQtModelIndex();
    return TQtModelIndex(row, column);
}

int TQtAbstractListModel::row(const TQtModelIndex& index) const
{
    if (!index.isValid() || index.nodeId < 0 || index.nodeId >= rowCount()) return -1;
    return index.nodeId;
}

TQVariant TQtAbstractListModel::data(const TQtModelIndex& index) const
{
    if (!index.isValid()) return TQVariant();
    return data(index.nodeId, index.column);
}

TQPixmap TQtAbstractListModel::decoration(const TQtModelIndex& index) const
{
    if (!index.isValid()) return TQPixmap();
    return decoration(index.nodeId, index.column);
}

TQtCellStyle TQtAbstractListModel::cellStyle(const TQtModelIndex& index) const
{
    if (!index.isValid()) return TQtCellStyle();
    return cellStyle(index.nodeId, index.column);
}

TQtCellStyle TQtAbstractListModel::cellStyle(const TQtModelIndex& index, const TQString& cellText) const
{
    if (!index.isValid()) return TQtCellStyle();
    return cellStyle(index.nodeId, index.column, cellText);
}

#include "tqtabstractlistmodel.moc"
