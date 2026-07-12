#include "tqtmvctableview.h"
#include "tqtcommon_p.h"
#include <ntqpainter.h>
#include <ntqheader.h>
#include <ntqpalette.h>
#include <ntqrect.h>
#include <ntqevent.h>
#include <ntqimage.h>
#include <ntqlabel.h>
#include <ntqtimer.h>
#include <ntqframe.h>
#include <ntqlayout.h>
#include <ntqscrollbar.h>
#include "tde_icon_loader.h"
#include <algorithm>  // std::sort

// ============================================================================
// Sort functors (C++98) — inlineable by compiler, no global state
// ============================================================================

struct SortEntry {
    double  numVal;
    TQString strVal;
    bool    isNum;
};

// Direct comparator for numeric columns — reads cache via visible row mapping
struct NumericComparatorDirect {
    const TQValueVector<int>& visible;
    const TQValueVector<double>& cache;
    bool ascending;
    NumericComparatorDirect(const TQValueVector<int>& vis,
                           const TQValueVector<double>& c, bool asc)
        : visible(vis), cache(c), ascending(asc) {}
    bool operator()(int a, int b) const {
        double va = cache[visible[a]];
        double vb = cache[visible[b]];
        return ascending ? va < vb : va > vb;
    }
};

// Direct comparator for mixed columns — reads cache via visible row mapping
struct MixedComparatorDirect {
    const TQValueVector<int>& visible;
    const TQValueVector<double>& numCache;
    const TQValueVector<TQString>& strCache;
    const TQValueVector<bool>& isNumCache;
    bool ascending;
    MixedComparatorDirect(const TQValueVector<int>& vis,
                         const TQValueVector<double>& nc,
                         const TQValueVector<TQString>& sc,
                         const TQValueVector<bool>& inc, bool asc)
        : visible(vis), numCache(nc), strCache(sc), isNumCache(inc), ascending(asc) {}
    bool operator()(int a, int b) const {
        int ma = visible[a], mb = visible[b];
        if (isNumCache[ma] && isNumCache[mb]) {
            return ascending ? numCache[ma] < numCache[mb]
                             : numCache[ma] > numCache[mb];
        }
        int cmp = TQString::localeAwareCompare(strCache[ma], strCache[mb]);
        return ascending ? cmp < 0 : cmp > 0;
    }
};

// Local comparator for cache-miss numeric path (indexed by position)
struct NumericComparator {
    const TQValueVector<double>& cache;
    bool ascending;
    NumericComparator(const TQValueVector<double>& c, bool asc) : cache(c), ascending(asc) {}
    bool operator()(int a, int b) const {
        return ascending ? cache[a] < cache[b] : cache[a] > cache[b];
    }
};

// ============================================================================
// TQtMvcTableView
// ============================================================================

TQtMvcTableView::TQtMvcTableView(TQWidget* parent)
    : TQTable(0, 0, parent),
      m_model(0),
      m_sortingEnabled(true),
      m_sortColumn(-1),
      m_sortAscending(true),
      m_sortCacheColumn(-1),
      m_sortCacheAllNumeric(false),
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

    // Connect header click for sorting
    connect(horizontalHeader(), SIGNAL(clicked(int)), this, SLOT(onHeaderClicked(int)));

    m_searchWidget = 0;
    m_searchIconLabel = 0;
    m_searchTextLabel = 0;
    m_searchTimer = new TQTimer(this);
    connect(m_searchTimer, SIGNAL(timeout()), this, SLOT(hideSearch()));
}

TQtMvcTableView::~TQtMvcTableView()
{
}

void TQtMvcTableView::setModel(TQtAbstractListModel* model)
{
    if (m_model) {
        disconnect(m_model, 0, this, 0);
    }

    m_model = model;

    if (m_model) {
        connect(m_model, SIGNAL(dataChanged(int,int)),  this, SLOT(onDataChanged(int,int)));
        connect(m_model, SIGNAL(rowsInserted(int,int)), this, SLOT(onRowsInserted(int,int)));
        connect(m_model, SIGNAL(rowsRemoved(int,int)),  this, SLOT(onRowsRemoved(int,int)));
        connect(m_model, SIGNAL(modelReset()),          this, SLOT(onModelReset()));

        syncColumns();
        rebuildIndex();
    } else {
        m_visibleRows.clear();
        setNumRows(0);
        setNumCols(0);
    }
}

TQtAbstractListModel* TQtMvcTableView::model() const
{
    return m_model;
}

void TQtMvcTableView::setSortingEnabled(bool enabled)
{
    m_sortingEnabled = enabled;
    horizontalHeader()->setClickEnabled(enabled);
}

void TQtMvcTableView::sortByColumn(int column, bool ascending)
{
    m_sortColumn = column;
    m_sortAscending = ascending;
    rebuildIndex();
}

void TQtMvcTableView::setFilterText(const TQString& text)
{
    m_filterText = text.lower();
    rebuildIndex();
}

void TQtMvcTableView::setColumnFilter(int column, const TQString& matchValue)
{
    m_filterColumn = column;
    m_filterColumnValue = matchValue;
    rebuildIndex();
}

int TQtMvcTableView::modelRow(int displayRow) const
{
    if (displayRow < 0 || displayRow >= (int)m_visibleRows.size())
        return -1;
    return m_visibleRows[displayRow];
}

int TQtMvcTableView::selectedModelRow() const
{
    int dispRow = currentRow();
    if (dispRow < 0 || dispRow >= (int)m_visibleRows.size())
        return -1;
    return m_visibleRows[dispRow];
}

int TQtMvcTableView::displayRowForModel(int mRow) const
{
    if (mRow < 0 || mRow >= (int)m_reverseIndex.size()) return -1;
    return m_reverseIndex[mRow];
}

void TQtMvcTableView::rebuildReverseIndex()
{
    int total = m_model ? m_model->rowCount() : 0;
    m_reverseIndex.resize(total);
    for (int i = 0; i < total; ++i)
        m_reverseIndex[i] = -1;
    for (int i = 0; i < (int)m_visibleRows.size(); ++i)
        m_reverseIndex[m_visibleRows[i]] = i;
}

void TQtMvcTableView::selectModelRow(int mRow)
{
    int dRow = displayRowForModel(mRow);
    if (dRow >= 0) {
        setCurrentCell(dRow, 0);
        selectRow(dRow);
    }
}

void TQtMvcTableView::syncColumns()
{
    if (!m_model) return;
    int cols = m_model->columnCount();
    setNumCols(cols);
    for (int c = 0; c < cols; ++c) {
        horizontalHeader()->setLabel(c, m_model->headerData(c));
    }
}

void TQtMvcTableView::rebuildIndex()
{
    blockPainting();

    if (!m_model) {
        m_visibleRows.clear();
        setNumRows(0);
        unblockPainting();
        return;
    }

    int total = m_model->rowCount();
    int cols  = m_model->columnCount();

    // --- Step 1: filter (using zero-allocation containsCI) ---
    m_visibleRows.clear();
    bool hasTextFilter = !m_filterText.isEmpty();
    bool hasColFilter = (m_filterColumn >= 0 && m_filterColumn < cols);

    if (!hasTextFilter && !hasColFilter) {
        m_visibleRows.resize(total);
        for (int i = 0; i < total; ++i)
            m_visibleRows[i] = i;
    } else {
        m_visibleRows.reserve(total);
        for (int i = 0; i < total; ++i) {
            // Column exact-match filter
            if (hasColFilter) {
                if (m_model->data(i, m_filterColumn).toString() != m_filterColumnValue) {
                    continue;
                }
            }

            // Global text filter
            if (hasTextFilter) {
                bool matchText = false;
                for (int c = 0; c < cols; ++c) {
                    if (containsCI(m_model->data(i, c).toString(), m_filterText)) {
                        matchText = true;
                        break;
                    }
                }
                if (!matchText) continue;
            }

            m_visibleRows.push_back(i);
        }
    }

    // --- Step 2: sort with pre-extracted keys + std::sort (inlineable) ---
    if (m_sortColumn >= 0 && m_sortColumn < cols && (int)m_visibleRows.size() > 1) {
        int n = (int)m_visibleRows.size();

        // Build index array [0..n-1]
        TQValueVector<int> sortIdx(n);
        for (int i = 0; i < n; ++i) sortIdx[i] = i;

        bool cacheHit = (m_sortCacheColumn == m_sortColumn);

        if (cacheHit && m_sortCacheAllNumeric) {
            // Cache hit (numeric) — direct comparator, zero copies
            std::stable_sort(sortIdx.begin(), sortIdx.end(),
                      NumericComparatorDirect(m_visibleRows, m_sortCacheNum,
                                             m_sortAscending));

        } else if (cacheHit) {
            // Cache hit (mixed) — direct comparator, zero copies
            std::stable_sort(sortIdx.begin(), sortIdx.end(),
                      MixedComparatorDirect(m_visibleRows,
                                           m_sortCacheNum, m_sortCacheStr,
                                           m_sortCacheIsNum, m_sortAscending));

        } else {
            // Cache miss — extract keys and populate cache
            int totalRows = m_model->rowCount();
            m_sortCacheNum.resize(totalRows);
            m_sortCacheStr.resize(totalRows);
            m_sortCacheIsNum.resize(totalRows);

            bool allNumeric = true;
            TQValueVector<double> numCache(n);
            for (int i = 0; i < n; ++i) {
                bool ok;
                TQString s = m_model->data(m_visibleRows[i], m_sortColumn).toString();
                double d = s.toDouble(&ok);
                numCache[i] = d;
                int mRow = m_visibleRows[i];
                m_sortCacheNum[mRow] = d;
                m_sortCacheStr[mRow] = s;
                m_sortCacheIsNum[mRow] = ok;
                if (!ok) allNumeric = false;
            }

            m_sortCacheColumn = m_sortColumn;
            m_sortCacheAllNumeric = allNumeric;

            if (allNumeric) {
                std::stable_sort(sortIdx.begin(), sortIdx.end(),
                          NumericComparator(numCache, m_sortAscending));
            } else {
                // Use direct comparator on freshly populated cache
                std::stable_sort(sortIdx.begin(), sortIdx.end(),
                          MixedComparatorDirect(m_visibleRows,
                                               m_sortCacheNum, m_sortCacheStr,
                                               m_sortCacheIsNum, m_sortAscending));
            }
        }

        TQValueVector<int> reordered(n);
        for (int i = 0; i < n; ++i)
            reordered[i] = m_visibleRows[sortIdx[i]];
        m_visibleRows = reordered;
    }

    // --- Step 3: sync table widget ---
    rebuildReverseIndex();
    int n = (int)m_visibleRows.size();
    setNumRows(n);
    for (int i = 0; i < n; ++i) {
        setRowHeight(i, 28);
    }
    unblockPainting();
}

// ============================================================================
// paintCell — maps display row → model row, then paints
// ============================================================================

void TQtMvcTableView::paintCell(TQPainter* p, int row, int col,
                                const TQRect& cr, bool selected)
{
    int mRow = modelRow(row);
    TQColorGroup cg = colorGroup();

    // Fetch data and style in a single pass (1 data() call instead of 2)
    TQString text;
    TQtCellStyle style;
    if (m_model && mRow >= 0) {
        TQVariant v = m_model->data(mRow, col);
        if (!v.isNull()) text = v.toString();
        style = m_model->cellStyle(mRow, col, text);
    }

    // Background
    TQColor bgColor;
    if (selected) {
        bgColor = cg.highlight();
    } else if (style.hasBackground) {
        bgColor = style.background;
    } else {
        bgColor = cg.base();
    }
    p->fillRect(0, 0, cr.width(), cr.height(), bgColor);

    // Foreground
    TQColor fgColor = selected ? cg.highlightedText()
                    : (style.hasForeground ? style.foreground : cg.text());
    p->setPen(fgColor);

    // Font — only copy and set if style modifies it
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

    // Icon + Text
    int textLeft = 3;
    if (m_model && mRow >= 0) {
        // Check for icon from decoration() or style
        TQPixmap px;
        if (style.hasIcon) {
            px = style.icon;
        } else {
            px = m_model->decoration(mRow, col);
        }
        if (!px.isNull()) {
            int iconH = TQMIN(px.height(), cr.height() - 2);
            int iconW = px.width() * iconH / TQMAX(px.height(), 1);
            // Use cached scaled version (avoid smoothScale at every repaint)
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

    // Text — reuse already-fetched string
    if (!text.isEmpty()) {
        int align = style.hasAlignment ? style.alignment
                                       : (int)(TQt::AlignLeft | TQt::AlignVCenter);
        TQRect textRect(textLeft, 0, cr.width() - textLeft - 3, cr.height());
        p->drawText(textRect, align, text);
    }

    /* No grid lines */

    if (fontChanged) {
        p->setFont(origFont);
    }
}

// ============================================================================
// Model signal handlers — incremental updates (no full re-sort)
// ============================================================================

void TQtMvcTableView::invalidateSortCache()
{
    m_sortCacheColumn = -1;
    m_sortCacheNum.clear();
    m_sortCacheStr.clear();
    m_sortCacheIsNum.clear();
}

void TQtMvcTableView::onDataChanged(int rowStart, int rowEnd)
{
    invalidateSortCache();

    // If filter or sort is active, data change may affect visibility/order
    if (!m_filterText.isEmpty() || m_sortColumn >= 0) {
        rebuildIndex();
        return;
    }

    // No filter/sort: repaint only affected display rows
    int cols = m_model ? m_model->columnCount() : 0;
    for (int mRow = rowStart; mRow <= rowEnd; ++mRow) {
        int dRow = displayRowForModel(mRow);
        if (dRow < 0) continue;
        for (int c = 0; c < cols; ++c)
            updateCell(dRow, c);
    }
}

void TQtMvcTableView::onRowsInserted(int row, int count)
{
    invalidateSortCache();
    // Save the ORIGINAL model row of the selected item (before shift)
    int dispRow = currentRow();
    int origSelModel = -1;
    if (dispRow >= 0 && dispRow < (int)m_visibleRows.size())
        origSelModel = m_visibleRows[dispRow];

    // Compute adjusted model row (after shift)
    int selModel = origSelModel;
    if (selModel >= 0 && selModel >= row)
        selModel += count;

    rebuildIndex();

    if (selModel >= 0) selectModelRow(selModel);
}

void TQtMvcTableView::onRowsRemoved(int row, int count)
{
    invalidateSortCache();
    // Save selected model row
    int dispRow = currentRow();
    int selModel = -1;
    if (dispRow >= 0 && dispRow < (int)m_visibleRows.size())
        selModel = m_visibleRows[dispRow];

    // Adjust selection for removal
    if (selModel >= 0) {
        if (selModel >= row && selModel < row + count)
            selModel = -1; // deleted
        else if (selModel >= row + count)
            selModel -= count;
    }

    rebuildIndex();

    if (selModel >= 0) selectModelRow(selModel);
}

void TQtMvcTableView::onModelReset()
{
    invalidateSortCache();
    m_scaledIconCache.clear();
    rebuildIndex();
}

void TQtMvcTableView::onHeaderClicked(int col)
{
    if (!m_sortingEnabled || !m_model) return;

    int sx = contentsX();
    int sy = contentsY();
    int selModel = selectedModelRow();

    blockPainting();

    if (m_sortColumn == col) {
        m_sortAscending = !m_sortAscending;
    } else {
        m_sortColumn = col;
        m_sortAscending = true;
    }

    // Reset all labels to plain text and request header update
    int cols = m_model->columnCount();
    for (int c = 0; c < cols; ++c) {
        horizontalHeader()->setLabel(c, m_model->headerData(c));
    }
    horizontalHeader()->update();

    rebuildIndex();

    if (selModel >= 0) {
        selectModelRow(selModel);
    } else {
        clearSelection();
    }

    setContentsPos(sx, sy);
    unblockPainting();
}

// ============================================================================
// Selection and interaction
// ============================================================================

void TQtMvcTableView::ensureModelRowVisible(int mRow)
{
    int dRow = displayRowForModel(mRow);
    if (dRow >= 0)
        ensureCellVisible(dRow, 0);
}

void TQtMvcTableView::currentChanged(int row, int col)
{
    TQTable::currentChanged(row, col);
    int mRow = modelRow(row);
    emit selectionChanged(mRow);
}

void TQtMvcTableView::contentsContextMenuEvent(TQContextMenuEvent* e)
{
    // e->pos() is already in contents coordinates (contentsContextMenuEvent)
    int dRow = rowAt(e->pos().y());
    int col  = columnAt(e->pos().x());
    int mRow = modelRow(dRow);
    emit rowContextMenuRequested(mRow, col, e->globalPos());
}

void TQtMvcTableView::viewportPaintEvent(TQPaintEvent* e)
{
    if (m_blockPaintingDepth > 0) return;
    TQTable::viewportPaintEvent(e);
}

void TQtMvcTableView::paintEmptyArea(TQPainter* p, int cx, int cy, int cw, int ch)
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
}bool TQtMvcTableView::eventFilter(TQObject* obj, TQEvent* ev)
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

void TQtMvcTableView::keyPressEvent(TQKeyEvent* e)
{
    // Ignore key modifiers except Shift (Shift is allowed for uppercase letters)
    if (e->state() & (TQt::ControlButton | TQt::AltButton | TQt::MetaButton)) {
        TQTable::keyPressEvent(e);
        return;
    }

    if (e->key() == TQt::Key_Escape) {
        if (!m_searchText.isEmpty()) {
            hideSearch();
            e->accept();
            return;
        }
    }
    else if (e->key() == TQt::Key_Backspace || e->key() == TQt::Key_BackSpace) {
        if (!m_searchText.isEmpty()) {
            m_searchText.truncate(m_searchText.length() - 1);
            updateSearch();
            e->accept();
            return;
        }
    }
    else if (!e->text().isEmpty() && e->text()[0].isPrint()) {
        m_searchText.append(e->text());
        updateSearch();
        e->accept();
        return;
    }

    TQTable::keyPressEvent(e);
}

void TQtMvcTableView::resizeEvent(TQResizeEvent* e)
{
    TQTable::resizeEvent(e);
    if (m_searchWidget && m_searchWidget->isVisible()) {
        int x = width() - m_searchWidget->width() - 10;
        int y = height() - m_searchWidget->height() - 10;
        if (verticalScrollBar() && verticalScrollBar()->isVisible()) {
            x -= verticalScrollBar()->width();
        }
        if (horizontalScrollBar() && horizontalScrollBar()->isVisible()) {
            y -= horizontalScrollBar()->height();
        }
        if (x < 0) x = 0;
        if (y < 0) y = 0;
        m_searchWidget->move(x, y);
    }
}

void TQtMvcTableView::updateSearch()
{
    if (m_searchText.isEmpty()) {
        hideSearch();
        return;
    }

    if (!m_searchWidget) {
        m_searchWidget = new TQFrame(this, "search_overlay");
        m_searchWidget->setFrameStyle(TQFrame::Box | TQFrame::Plain);
        m_searchWidget->setLineWidth(1);
        m_searchWidget->setPaletteBackgroundColor(colorGroup().background());
        m_searchWidget->setPaletteForegroundColor(colorGroup().foreground());

        TQHBoxLayout* layout = new TQHBoxLayout(m_searchWidget, 4, 6); // margin=4, spacing=6

        m_searchIconLabel = new TQLabel(m_searchWidget);
        TQPixmap pix = TdeIconLoader::loadSmallIcon("find");
        if (pix.isNull()) {
            pix.load("icons/find.png");
        }
        if (pix.isNull()) {
            pix.load("../icons/find.png");
        }
        if (!pix.isNull()) {
            m_searchIconLabel->setPixmap(pix);
            layout->addWidget(m_searchIconLabel);
        } else {
            delete m_searchIconLabel;
            m_searchIconLabel = 0;
        }

        m_searchTextLabel = new TQLabel(m_searchWidget);
        m_searchTextLabel->setPaletteForegroundColor(colorGroup().foreground());
        layout->addWidget(m_searchTextLabel);
    }

    if (m_searchTextLabel) {
        m_searchTextLabel->setText(m_searchText);
    }
    m_searchWidget->adjustSize();

    int x = width() - m_searchWidget->width() - 10;
    int y = height() - m_searchWidget->height() - 10;
    if (verticalScrollBar() && verticalScrollBar()->isVisible()) {
        x -= verticalScrollBar()->width();
    }
    if (horizontalScrollBar() && horizontalScrollBar()->isVisible()) {
        y -= horizontalScrollBar()->height();
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    m_searchWidget->move(x, y);
    m_searchWidget->show();
    m_searchWidget->raise();

    m_searchTimer->start(5000, true); // 5-second single shot timer

    if (m_model) {
        int matchRow = -1;
        for (int r = 0; r < numRows(); ++r) {
            int mRow = modelRow(r);
            if (mRow >= 0) {
                TQString name = m_model->data(mRow, 0).toString();
                if (name.startsWith(m_searchText, false)) { // case-insensitive prefix match
                    matchRow = r;
                    break;
                }
            }
        }
        if (matchRow >= 0) {
            setCurrentCell(matchRow, 0);
            selectRow(matchRow);
            ensureCellVisible(matchRow, 0);
        }
    }
}

void TQtMvcTableView::hideSearch()
{
    m_searchText = "";
    if (m_searchWidget) {
        m_searchWidget->hide();
    }
}


#include "tqtmvctableview.moc"
