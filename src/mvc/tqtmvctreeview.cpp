#include "tqtmvctreeview.h"
#include "tqttreestore.h"
#include "tqtcommon_p.h"
#include <ntqpainter.h>
#include <ntqheader.h>
#include <ntqpalette.h>
#include <ntqrect.h>
#include <ntqevent.h>
#include <ntqimage.h>
#include <algorithm>

static const unsigned char right_arrow_png_data[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20,
    0x08, 0x03, 0x00, 0x00, 0x00, 0x44, 0xa4, 0x8a, 0xc6, 0x00, 0x00, 0x00,
    0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0b, 0x13, 0x00, 0x00, 0x0b,
    0x13, 0x01, 0x00, 0x9a, 0x9c, 0x18, 0x00, 0x00, 0x00, 0x39, 0x50, 0x4c,
    0x54, 0x45, 0xff, 0xff, 0xff, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00, 0x01, 0x48,
    0xc7, 0x0f, 0x63, 0x00, 0x00, 0x00, 0x12, 0x74, 0x52, 0x4e, 0x53, 0x00,
    0x00, 0x1b, 0x3d, 0x70, 0xa8, 0xe4, 0x3c, 0xb7, 0xfe, 0x9a, 0x25, 0x3e,
    0x85, 0x84, 0x50, 0x8c, 0x18, 0x0f, 0xcd, 0x3b, 0xff, 0x00, 0x00, 0x00,
    0x81, 0x49, 0x44, 0x41, 0x54, 0x78, 0x5e, 0xad, 0xcd, 0x49, 0x0e, 0xc4,
    0x20, 0x10, 0x04, 0xc1, 0x2e, 0x3c, 0xe0, 0x7d, 0x96, 0xfe, 0xff, 0x63,
    0x7d, 0xc1, 0x92, 0xc9, 0x16, 0xe2, 0x32, 0x75, 0xcd, 0x90, 0xca, 0xc6,
    0x93, 0xb4, 0x95, 0x59, 0xcf, 0x11, 0x64, 0x77, 0x9f, 0xfa, 0x60, 0x5f,
    0xdc, 0x21, 0xf0, 0xb1, 0x3a, 0x04, 0x41, 0x72, 0x0a, 0x1b, 0x09, 0x1b,
    0x09, 0x1b, 0x08, 0x02, 0x0a, 0x02, 0x51, 0x44, 0xd0, 0x88, 0x23, 0x82,
    0xba, 0x5b, 0x7c, 0x0d, 0x13, 0xc5, 0x49, 0x10, 0xc4, 0xbb, 0x07, 0xf4,
    0xa9, 0x62, 0xef, 0x88, 0xa9, 0xf6, 0x97, 0x08, 0xd8, 0x09, 0xd8, 0x09,
    0xda, 0xfe, 0x53, 0x00, 0x4d, 0x4f, 0x8a, 0x80, 0x3d, 0x02, 0x76, 0x02,
    0xf6, 0x08, 0xd8, 0x09, 0x66, 0x76, 0x82, 0xc2, 0x4e, 0xb0, 0xb1, 0x13,
    0x28, 0xb3, 0x13, 0x68, 0x59, 0x93, 0x00, 0xfe, 0xb8, 0x0b, 0x6f, 0xc2,
    0x0e, 0x9e, 0x65, 0x45, 0xd7, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45,
    0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

static const unsigned char down_arrow_png_data[] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x20,
    0x08, 0x06, 0x00, 0x00, 0x00, 0x73, 0x7a, 0x7a, 0xf4, 0x00, 0x00, 0x00,
    0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0b, 0x13, 0x00, 0x00, 0x0b,
    0x13, 0x01, 0x00, 0x9a, 0x9c, 0x18, 0x00, 0x00, 0x00, 0xd5, 0x49, 0x44,
    0x41, 0x54, 0x58, 0x09, 0xed, 0xd6, 0xc1, 0x09, 0xc2, 0x30, 0x18, 0x86,
    0xe1, 0xbf, 0x9d, 0x42, 0xe8, 0xd9, 0x29, 0xac, 0xce, 0xe0, 0x28, 0x0e,
    0xa1, 0x37, 0x4f, 0x1e, 0x3b, 0x41, 0x37, 0x70, 0x21, 0xe7, 0x68, 0xfc,
    0x2b, 0x14, 0xc2, 0x27, 0xa1, 0xf8, 0x26, 0xd8, 0x4b, 0x02, 0xaf, 0x78,
    0x28, 0xf9, 0x1e, 0x7a, 0xaa, 0x85, 0x10, 0x3e, 0x99, 0x35, 0xff, 0x6a,
    0xbf, 0xfc, 0x9f, 0x77, 0x5b, 0xfb, 0x3e, 0x83, 0x77, 0xf0, 0x7a, 0x2b,
    0x7b, 0x3a, 0x6f, 0xf2, 0x1e, 0x16, 0x1d, 0x7d, 0x03, 0xa3, 0x17, 0xa2,
    0x8e, 0xa2, 0xa7, 0x75, 0x72, 0xef, 0xb8, 0xbc, 0x81, 0x18, 0xf0, 0x94,
    0x87, 0xb2, 0x11, 0x32, 0xae, 0x0d, 0x0a, 0x98, 0xbc, 0x50, 0x12, 0x21,
    0xe3, 0xda, 0x4b, 0x00, 0xf2, 0x70, 0x3e, 0x62, 0xed, 0xbe, 0x5e, 0x01,
    0x25, 0x10, 0x3f, 0xdd, 0xa3, 0x00, 0x80, 0xe0, 0xe3, 0x0a, 0x80, 0x97,
    0xf0, 0xf1, 0x34, 0x80, 0x22, 0x00, 0x5a, 0x01, 0x08, 0x01, 0xc7, 0xd3,
    0x00, 0x80, 0x38, 0x83, 0xf1, 0x04, 0x00, 0x20, 0xd0, 0x38, 0x00, 0x28,
    0x82, 0x8f, 0x53, 0x80, 0x20, 0xf8, 0x38, 0x00, 0x68, 0xbb, 0x95, 0xf1,
    0x93, 0x67, 0xa5, 0x01, 0xda, 0x25, 0x31, 0x7e, 0xf7, 0x2c, 0x1f, 0xc0,
    0x10, 0x37, 0xcf, 0xf2, 0x00, 0x1c, 0x71, 0xf5, 0xac, 0x00, 0x80, 0x7f,
    0x62, 0x51, 0x40, 0x33, 0xff, 0x6c, 0x79, 0x5a, 0xdb, 0xf8, 0x54, 0x40,
    0x05, 0x54, 0x40, 0x05, 0x54, 0xc0, 0x1b, 0x38, 0xe1, 0x6f, 0x8e, 0x3b,
    0xfb, 0x4f, 0x51, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae,
    0x42, 0x60, 0x82,
};

static TQPixmap* rightArrowPixmap() {
    static TQPixmap* pm = NULL;
    if (!pm) {
        pm = new TQPixmap();
        TQImage img;
        if (img.loadFromData(right_arrow_png_data, sizeof(right_arrow_png_data), "PNG")) {
            img = img.smoothScale(12, 12);
            pm->convertFromImage(img);
        }
    }
    return pm;
}

static TQPixmap* downArrowPixmap() {
    static TQPixmap* pm = NULL;
    if (!pm) {
        pm = new TQPixmap();
        TQImage img;
        if (img.loadFromData(down_arrow_png_data, sizeof(down_arrow_png_data), "PNG")) {
            img = img.smoothScale(12, 12);
            pm->convertFromImage(img);
        }
    }
    return pm;
}

TQtMvcTreeView::TQtMvcTreeView(TQWidget* parent)
    : TQTable(0, 0, parent),
      m_model(0),
      m_sortingEnabled(true),
      m_sortColumn(-1),
      m_sortAscending(true),
      m_filterColumn(-1),
      m_blockPaintingDepth(0)
{
    setReadOnly(true);
    setSelectionMode(TQTable::SingleRow);

    // Install event filter to paint custom sort arrows after text labels
    horizontalHeader()->installEventFilter(this);

    verticalHeader()->hide();
    setLeftMargin(0);
    setFocusStyle(TQTable::FollowStyle);
    setShowGrid(false);
    setFrameStyle(TQFrame::NoFrame);

    /* Disable viewport background auto-erase for smooth flicker-free rendering */
    if (viewport()) viewport()->setBackgroundMode(TQt::NoBackground);

    connect(horizontalHeader(), SIGNAL(clicked(int)), this, SLOT(onHeaderClicked(int)));
}

TQtMvcTreeView::~TQtMvcTreeView()
{
}

void TQtMvcTreeView::setModel(TQtAbstractItemModel* model)
{
    if (m_model) {
        disconnect(m_model, 0, this, 0);
    }

    m_model = model;

    if (m_model) {
        connect(m_model, SIGNAL(indexDataChanged(const TQtModelIndex&, const TQtModelIndex&)),
                this,    SLOT(onIndexDataChanged(const TQtModelIndex&, const TQtModelIndex&)));
        connect(m_model, SIGNAL(indexRowsInserted(const TQtModelIndex&, int, int)),
                this,    SLOT(onIndexRowsInserted(const TQtModelIndex&, int, int)));
        connect(m_model, SIGNAL(indexRowsRemoved(const TQtModelIndex&, int, int)),
                this,    SLOT(onIndexRowsRemoved(const TQtModelIndex&, int, int)));
        connect(m_model, SIGNAL(modelReset()), this, SLOT(onModelReset()));
        connect(m_model, SIGNAL(layoutChanged()), this, SLOT(onLayoutChanged()));

        syncColumns();
        rebuildIndex();
    } else {
        m_visibleRows.clear();
        m_nodeToDisplay.clear();
        setNumRows(0);
        setNumCols(0);
    }
}

TQtAbstractItemModel* TQtMvcTreeView::model() const
{
    return m_model;
}

void TQtMvcTreeView::setSortingEnabled(bool enabled)
{
    m_sortingEnabled = enabled;
    horizontalHeader()->setClickEnabled(enabled);
}

void TQtMvcTreeView::sortByColumn(int column, bool ascending)
{
    m_sortColumn = column;
    m_sortAscending = ascending;

    TQtTreeStore* tree = NULL;
    if (m_model && m_model->inherits("TQtTreeStore"))
        tree = static_cast<TQtTreeStore*>(m_model);
    if (tree)
        tree->sortAllChildren(column, ascending);
    else
        rebuildIndex();
}

void TQtMvcTreeView::setFilterText(const TQString& text)
{
    m_filterText = text.lower();
    rebuildIndex();
}

void TQtMvcTreeView::setColumnFilter(int column, const TQString& matchValue)
{
    m_filterColumn = column;
    m_filterColumnValue = matchValue;
    rebuildIndex();
}

int TQtMvcTreeView::nodeId(int displayRow) const
{
    if (displayRow < 0 || displayRow >= (int)m_visibleRows.size()) return -1;
    return m_visibleRows[displayRow].nodeId;
}

int TQtMvcTreeView::depthAt(int displayRow) const
{
    if (displayRow < 0 || displayRow >= (int)m_visibleRows.size()) return 0;
    return m_visibleRows[displayRow].depth;
}

TQtModelIndex TQtMvcTreeView::selectedIndex() const
{
    int row = currentRow();
    int id = nodeId(row);
    if (id < 0) return TQtModelIndex();
    return TQtModelIndex(id, 0);
}

void TQtMvcTreeView::selectIndex(const TQtModelIndex& index)
{
    if (!index.isValid()) return;
    int row = displayRowForNode(index.nodeId);
    if (row >= 0) {
        /* Save scroll position to prevent auto-scroll */
        int sx = contentsX();
        int sy = contentsY();
        setCurrentCell(row, 0);
        selectRow(row);
        /* Restore scroll position — no auto-scroll on selection restore */
        setContentsPos(sx, sy);
    }
}

void TQtMvcTreeView::ensureIndexVisible(const TQtModelIndex& index)
{
    if (!index.isValid()) return;
    int row = displayRowForNode(index.nodeId);
    if (row >= 0)
        ensureCellVisible(row, 0);
}

bool TQtMvcTreeView::isExpanded(int nodeId) const
{
    TQMap<int, bool>::ConstIterator it = m_expanded.find(nodeId);
    if (it == m_expanded.end()) return false;
    return it.data();
}

void TQtMvcTreeView::setExpanded(int nodeId, bool expanded)
{
    if (expanded)
        m_expanded[nodeId] = true;
    else
        m_expanded.remove(nodeId);
    rebuildIndex();
}

void TQtMvcTreeView::expand(const TQtModelIndex& index)
{
    if (index.isValid()) {
        setExpanded(index.nodeId, true);
        emit expanded(index.nodeId);
    }
}

void TQtMvcTreeView::collapse(const TQtModelIndex& index)
{
    if (index.isValid()) {
        setExpanded(index.nodeId, false);
        emit collapsed(index.nodeId);
    }
}

void TQtMvcTreeView::toggleExpanded(int nodeId)
{
    bool next = !isExpanded(nodeId);
    setExpanded(nodeId, next);
    if (next)
        emit expanded(nodeId);
    else
        emit collapsed(nodeId);
}

static void expandAllRecursiveHelper(TQtAbstractItemModel* model, const TQtModelIndex& parent, TQMap<int, bool>& expandedMap) {
    int n = model->childCount(parent);
    for (int i = 0; i < n; ++i) {
        TQtModelIndex child = model->index(i, 0, parent);
        if (child.isValid()) {
            expandedMap[child.nodeId] = true;
            expandAllRecursiveHelper(model, child, expandedMap);
        }
    }
}

void TQtMvcTreeView::expandAll()
{
    if (!m_model) return;
    expandAllRecursiveHelper(m_model, TQtModelIndex(), m_expanded);
    rebuildIndex();
}

void TQtMvcTreeView::collapseAll()
{
    if (m_expanded.isEmpty()) return;
    m_expanded.clear();
    rebuildIndex();
}

bool TQtMvcTreeView::nodeMatchesFilter(int nodeId) const
{
    if (!m_model || nodeId < 0) return false;
    int cols = m_model->columnCount();

    if (m_filterColumn >= 0 && m_filterColumn < cols) {
        if (m_model->data(TQtModelIndex(nodeId, m_filterColumn)).toString() != m_filterColumnValue)
            return false;
    }

    if (m_filterText.isEmpty()) return true;

    for (int c = 0; c < cols; ++c) {
        if (containsCI(m_model->data(TQtModelIndex(nodeId, c)).toString(), m_filterText))
            return true;
    }
    return false;
}

bool TQtMvcTreeView::nodeVisibleInFilter(int nodeId) const
{
    if (!m_model || nodeId < 0) return false;
    if (nodeMatchesFilter(nodeId)) return true;

    if (m_filterText.isEmpty() &&
        (m_filterColumn < 0 || m_filterColumnValue.isEmpty()))
        return true;

    TQtModelIndex parentIdx(nodeId, 0);
    int n = m_model->childCount(parentIdx);
    for (int i = 0; i < n; ++i) {
        TQtModelIndex child = m_model->index(i, 0, parentIdx);
        if (child.isValid() && nodeVisibleInFilter(child.nodeId))
            return true;
    }
    return false;
}

void TQtMvcTreeView::appendVisibleSubtree(int nodeId, int depth)
{
    if (!m_model || nodeId < 0) return;
    if (!nodeVisibleInFilter(nodeId)) return;

    m_visibleRows.push_back(VisibleEntry(nodeId, depth));

    if (!isExpanded(nodeId)) return;

    TQtModelIndex parentIdx(nodeId, 0);
    int n = m_model->childCount(parentIdx);
    for (int i = 0; i < n; ++i) {
        TQtModelIndex child = m_model->index(i, 0, parentIdx);
        if (child.isValid())
            appendVisibleSubtree(child.nodeId, depth + 1);
    }
}

void TQtMvcTreeView::rebuildIndex()
{
    blockPainting();
    
    m_visibleRows.clear();
    m_nodeToDisplay.clear();

    if (!m_model) {
        setNumRows(0);
        unblockPainting();
        return;
    }

    int roots = m_model->childCount(TQtModelIndex());
    for (int i = 0; i < roots; ++i) {
        TQtModelIndex idx = m_model->index(i, 0, TQtModelIndex());
        if (idx.isValid())
            appendVisibleSubtree(idx.nodeId, 0);
    }

    rebuildReverseIndex();
    int n = (int)m_visibleRows.size();
    setNumRows(n);
    for (int i = 0; i < n; ++i) {
        setRowHeight(i, 28);
    }

    unblockPainting();
}

void TQtMvcTreeView::rebuildReverseIndex()
{
    m_nodeToDisplay.clear();
    for (int i = 0; i < (int)m_visibleRows.size(); ++i)
        m_nodeToDisplay[m_visibleRows[i].nodeId] = i;
}

int TQtMvcTreeView::displayRowForNode(int nodeId) const
{
    TQMap<int, int>::ConstIterator it = m_nodeToDisplay.find(nodeId);
    if (it == m_nodeToDisplay.end()) return -1;
    return it.data();
}

void TQtMvcTreeView::syncColumns()
{
    if (!m_model) return;
    int cols = m_model->columnCount();
    setNumCols(cols);
    for (int c = 0; c < cols; ++c)
        horizontalHeader()->setLabel(c, m_model->headerData(c));
}

int TQtMvcTreeView::contentLeft(int displayRow, int col, bool) const
{
    int left = 3;
    if (col == 0) {
        left += depthAt(displayRow) * IndentPerLevel;
        left += ToggleSize + 2;
    }
    return left;
}

void TQtMvcTreeView::drawExpandToggle(TQPainter* p, int x, int y, int h,
                                      bool isOpen) const
{
    TQPixmap* pm = isOpen ? downArrowPixmap() : rightArrowPixmap();
    if (pm && !pm->isNull()) {
        int cy = y + (h - pm->height()) / 2;
        p->drawPixmap(x, cy, *pm);
    }
}

bool TQtMvcTreeView::hitExpandToggle(int displayRow, int x) const
{
    int id = nodeId(displayRow);
    if (id < 0 || !m_model) return false;
    if (!m_model->hasChildren(TQtModelIndex(id, 0))) return false;
    int toggleX = depthAt(displayRow) * IndentPerLevel + 2;
    return x >= toggleX && x < toggleX + ToggleSize;
}

void TQtMvcTreeView::paintCell(TQPainter* p, int row, int col,
                               const TQRect& cr, bool selected)
{
    int id = nodeId(row);
    TQtModelIndex index(id, col);
    TQColorGroup cg = colorGroup();

    TQString text;
    TQtCellStyle style;
    bool hasChildren = false;
    if (m_model && id >= 0) {
        hasChildren = m_model->hasChildren(TQtModelIndex(id, 0));
        TQVariant v = m_model->data(index);
        if (!v.isNull()) text = v.toString();
        style = m_model->cellStyle(index, text);
    }

    TQColor bgColor;
    if (selected) {
        bgColor = cg.highlight();
    } else if (style.hasBackground) {
        bgColor = style.background;
    } else {
        bgColor = cg.base();
    }
    p->fillRect(0, 0, cr.width(), cr.height(), bgColor);

    TQColor fgColor = selected ? cg.highlightedText()
                    : (style.hasForeground ? style.foreground : cg.text());
    p->setPen(fgColor);

    bool fontChanged = false;
    TQFont origFont;
    if (style.hasBold || style.hasItalic || style.hasUnderline) {
        origFont = p->font();
        TQFont f = origFont;
        if (style.hasBold)      f.setBold(style.bold);
        if (style.hasItalic)    f.setItalic(style.italic);
        if (style.hasUnderline) f.setUnderline(style.underline);
        p->setFont(f);
        fontChanged = true;
    }

    if (col == 0 && hasChildren) {
        int toggleX = depthAt(row) * IndentPerLevel + 2;
        drawExpandToggle(p, toggleX, 0, cr.height(), isExpanded(id));
    }

    int textLeft = contentLeft(row, col, hasChildren);

    if (m_model && id >= 0) {
        TQPixmap px;
        if (style.hasIcon) {
            px = style.icon;
        } else {
            px = m_model->decoration(index);
        }
        if (!px.isNull()) {
            int iconH = TQMIN(px.height(), cr.height() - 2);
            int iconW = px.width() * iconH / TQMAX(px.height(), 1);
            if (iconW != px.width() || iconH != px.height()) {
                long cacheKey = ((long)px.serialNumber() << 16) | (long)iconH;
                TQMap<long, TQPixmap>::Iterator it = m_scaledIconCache.find(cacheKey);
                if (it != m_scaledIconCache.end()) {
                    px = it.data();
                } else {
                    TQImage img = px.convertToImage().smoothScale(iconW, iconH);
                    px.convertFromImage(img);
                    m_scaledIconCache[cacheKey] = px;
                }
            }
            int iconY = (cr.height() - iconH) / 2;
            p->drawPixmap(textLeft, iconY, px);
            textLeft += iconW + 3;
        }
    }

    if (!text.isEmpty()) {
        int align = style.hasAlignment ? style.alignment
                                       : (int)(TQt::AlignLeft | TQt::AlignVCenter);
        TQRect textRect(textLeft, 0, cr.width() - textLeft - 3, cr.height());
        p->drawText(textRect, align, text);
    }

    // Draw vertical separator lines on the left and right edges for columns with conditional coloring
    TQString colLabel = horizontalHeader()->label(col);
    if (colLabel == "CPU" || colLabel == "Memory" || colLabel == "GPU" || colLabel == "Prio") {
        p->save();
        p->setPen(TQPen(TQColor(215, 215, 215), 1));
        p->drawLine(0, 0, 0, cr.height());
        p->drawLine(cr.width() - 1, 0, cr.width() - 1, cr.height());
        p->restore();
    }

    if (fontChanged)
        p->setFont(origFont);
}

void TQtMvcTreeView::invalidateSortCache()
{
}

void TQtMvcTreeView::onIndexDataChanged(const TQtModelIndex& topLeft,
                                       const TQtModelIndex& bottomRight)
{
    Q_UNUSED(bottomRight);
    if (!m_filterText.isEmpty() || m_filterColumn >= 0 || m_sortColumn >= 0) {
        rebuildIndex();
        return;
    }

    int dRow = displayRowForNode(topLeft.nodeId);
    if (dRow < 0) return;
    int cols = m_model ? m_model->columnCount() : 0;
    for (int c = topLeft.column; c <= bottomRight.column && c < cols; ++c)
        updateCell(dRow, c);
}

void TQtMvcTreeView::onIndexRowsInserted(const TQtModelIndex& /*parent*/,
                                         int /*first*/, int /*count*/)
{
    rebuildIndex();
}

void TQtMvcTreeView::onIndexRowsRemoved(const TQtModelIndex& /*parent*/,
                                        int /*first*/, int /*count*/)
{
    rebuildIndex();
}

void TQtMvcTreeView::onModelReset()
{
    m_scaledIconCache.clear();
    rebuildIndex();
}

void TQtMvcTreeView::onLayoutChanged()
{
    rebuildIndex();
}

void TQtMvcTreeView::onHeaderClicked(int col)
{
    if (!m_sortingEnabled || !m_model) return;

    int sx = contentsX();
    int sy = contentsY();
    TQtModelIndex selIdx = selectedIndex();

    blockPainting();

    if (m_sortColumn == col)
        m_sortAscending = !m_sortAscending;
    else {
        m_sortColumn = col;
        m_sortAscending = true;
    }

    int cols = m_model->columnCount();
    for (int c = 0; c < cols; ++c) {
        horizontalHeader()->setLabel(c, m_model->headerData(c));
    }
    horizontalHeader()->update();

    sortByColumn(m_sortColumn, m_sortAscending);

    if (selIdx.isValid()) {
        selectIndex(selIdx);
    } else {
        clearSelection();
    }

    setContentsPos(sx, sy);
    unblockPainting();
}

void TQtMvcTreeView::currentChanged(int row, int col)
{
    TQTable::currentChanged(row, col);
    emit selectionChanged(selectedIndex());
}

void TQtMvcTreeView::contentsContextMenuEvent(TQContextMenuEvent* e)
{
    int dRow = rowAt(e->pos().y());
    int col  = columnAt(e->pos().x());
    int id = nodeId(dRow);
    TQtModelIndex index(id, col);
    emit rowContextMenuRequested(index, col, e->globalPos());
}

void TQtMvcTreeView::contentsMousePressEvent(TQMouseEvent* e)
{
    int dRow = rowAt(e->pos().y());
    if (dRow >= 0) {
        int id = nodeId(dRow);

        if (hitExpandToggle(dRow, e->pos().x())) {
            toggleExpanded(id);
            e->accept();
            return;
        }

        /* Parent/group row: first click selects, second click toggles expand. */
        if (e->button() == LeftButton && m_model && id >= 0
            && m_model->hasChildren(TQtModelIndex(id, 0))
            && currentRow() == dRow) {
            toggleExpanded(id);
            e->accept();
            return;
        }
    }
    TQTable::contentsMousePressEvent(e);
}

void TQtMvcTreeView::viewportPaintEvent(TQPaintEvent* e)
{
    if (m_blockPaintingDepth > 0) return;
    TQTable::viewportPaintEvent(e);
}

void TQtMvcTreeView::paintEmptyArea(TQPainter* p, int cx, int cy, int cw, int ch)
{
    int totalW = 0;
    if (numCols() > 0) {
        totalW = columnPos(numCols() - 1) + columnWidth(numCols() - 1);
    }
    int totalH = 0;
    if (numRows() > 0) {
        totalH = rowPos(numRows() - 1) + rowHeight(numRows() - 1);
    }

    TQColor baseColor = colorGroup().base();

    // Clear area to the right of columns
    if (cx + cw > totalW) {
        int startX = TQMAX(cx, totalW);
        p->fillRect(startX, cy, cx + cw - startX, ch, baseColor);
    }
    // Clear area below rows
    if (cy + ch > totalH) {
        int startY = TQMAX(cy, totalH);
        p->fillRect(cx, startY, cw, cy + ch - startY, baseColor);
    }
}bool TQtMvcTreeView::eventFilter(TQObject* obj, TQEvent* ev)
{
    if (obj == horizontalHeader() && ev->type() == TQEvent::Paint) {
        // Temporarily remove to prevent recursion
        horizontalHeader()->removeEventFilter(this);
        TQApplication::sendEvent(horizontalHeader(), ev);
        horizontalHeader()->installEventFilter(this);

        // Draw custom sort indicator
        if (m_sortColumn >= 0 && m_sortColumn < numCols()) {
            TQString labelText = m_model ? m_model->headerData(m_sortColumn) : TQString();
            if (!labelText.isEmpty()) {
                TQPainter p(horizontalHeader());
                int textW = p.fontMetrics().width(labelText);
                TQPixmap* pm = m_sortAscending ? upArrowHeaderPixmap() : downArrowHeaderPixmap();
                if (pm && !pm->isNull()) {
                    int pmW = pm->width();
                    int pmH = pm->height();
                    
                    int sectionX = horizontalHeader()->sectionPos(m_sortColumn);
                    int sectionW = horizontalHeader()->sectionSize(m_sortColumn);
                    int sectionH = horizontalHeader()->height();
                    
                    int textX = sectionX + 6;
                    int iconX = textX + textW + 4;
                    int iconY = (sectionH - pmH) / 2;
                    
                    if (iconX + pmW <= sectionX + sectionW - 4) {
                        p.drawPixmap(iconX, iconY, *pm);
                    }
                }
            }
        }
        return true;
    }
    return TQTable::eventFilter(obj, ev);
}


#include "tqtmvctreeview.moc"
