#ifndef TQTABSTRACTLISTMODEL_H
#define TQTABSTRACTLISTMODEL_H

#include "tqtabstractitemmodel.h"

/**
 * @brief Abstract base class for flat MVC list/table models.
 *
 * Subclass this (or use TQtListStore) to provide data to TQtMvcTableView.
 * Tree-aware defaults map nodeId to row index for TQtAbstractItemModel API.
 */
class TQtAbstractListModel : public TQtAbstractItemModel
{
    TQ_OBJECT

public:
    TQtAbstractListModel(TQObject* parent = 0) : TQtAbstractItemModel(parent) {}
    virtual ~TQtAbstractListModel() {}

    /** @brief Returns the total number of rows. */
    virtual int rowCount() const = 0;

    /**
     * @brief Returns the data for a given cell.
     * Return a null TQVariant() if the cell has no data.
     */
    virtual TQVariant data(int row, int column) const = 0;

    /**
     * @brief Returns the header label for a given column.
     * Override this to provide column titles.
     */
    virtual TQString headerData(int column) const;

    /**
     * @brief Returns an optional icon/pixmap for a given cell.
     * Return a null TQPixmap if no icon is needed. Override in subclass.
     */
    virtual TQPixmap decoration(int row, int column) const;

    /**
     * @brief Returns the effective visual style for a given cell.
     */
    virtual TQtCellStyle cellStyle(int row, int column) const;

    /**
     * @brief Overload accepting pre-fetched cell text (avoids double data() call).
     */
    virtual TQtCellStyle cellStyle(int row, int column, const TQString& cellText) const;

    // -- TQtAbstractItemModel bridge (flat defaults) --
    int childCount(const TQtModelIndex& parent) const;
    TQtModelIndex index(int row, int column, const TQtModelIndex& parent) const;
    int row(const TQtModelIndex& index) const;
    TQVariant data(const TQtModelIndex& index) const;
    TQPixmap decoration(const TQtModelIndex& index) const;
    TQtCellStyle cellStyle(const TQtModelIndex& index) const;
    TQtCellStyle cellStyle(const TQtModelIndex& index, const TQString& cellText) const;

signals:
    /** @brief Emitted when data in the given row range has changed. */
    void dataChanged(int rowStart, int rowEnd);

    /** @brief Emitted after rows have been inserted. */
    void rowsInserted(int row, int count);

    /** @brief Emitted after rows have been removed. */
    void rowsRemoved(int row, int count);
};

#endif // TQTABSTRACTLISTMODEL_H
