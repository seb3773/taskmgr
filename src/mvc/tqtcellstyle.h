#ifndef TQTCELLSTYLE_H
#define TQTCELLSTYLE_H

#include <ntqcolor.h>
#include <ntqfont.h>
#include <ntqpixmap.h>

/**
 * @brief Describes the visual style of a cell or column in TQtMvcTableView.
 *
 * A "null" style (the default) means "inherit from the view's palette".
 * A column style acts as the default for all cells in that column.
 * A cell style overrides the column style for a specific cell.
 */
struct TQtCellStyle
{
    // --- Validity flags (so we know what to apply) ---
    bool hasForeground;
    bool hasBackground;
    bool hasBold;
    bool hasItalic;
    bool hasUnderline;
    bool hasAlignment;
    bool hasIcon;

    TQColor foreground;
    TQColor background;
    bool bold;
    bool italic;
    bool underline;
    int   alignment;   // TQt::AlignLeft | TQt::AlignRight | TQt::AlignHCenter | TQt::AlignVCenter
    TQPixmap icon;

    TQtCellStyle()
        : hasForeground(false), hasBackground(false),
          hasBold(false), hasItalic(false), hasUnderline(false),
          hasAlignment(false), hasIcon(false),
          bold(false), italic(false), underline(false),
          alignment(TQt::AlignLeft | TQt::AlignVCenter)
    {}

    bool isNull() const {
        return !hasForeground && !hasBackground && !hasBold
            && !hasItalic && !hasUnderline && !hasAlignment && !hasIcon;
    }

    // Convenience builders (chainable)
    TQtCellStyle& setForeground(const TQColor& c) { foreground = c; hasForeground = true; return *this; }
    TQtCellStyle& setBackground(const TQColor& c) { background = c; hasBackground = true; return *this; }
    TQtCellStyle& setBold(bool v = true)      { bold = v;      hasBold = true;      return *this; }
    TQtCellStyle& setItalic(bool v = true)    { italic = v;    hasItalic = true;    return *this; }
    TQtCellStyle& setUnderline(bool v = true) { underline = v; hasUnderline = true; return *this; }
    TQtCellStyle& setAlignment(int a)         { alignment = a; hasAlignment = true; return *this; }
    TQtCellStyle& setIcon(const TQPixmap& px)   { icon = px;     hasIcon = true;      return *this; }

    /**
     * @brief Merges "overlay" onto this style.
     * Fields explicitly set in overlay take precedence over fields in this style.
     */
    TQtCellStyle merged(const TQtCellStyle& overlay) const {
        TQtCellStyle result = *this;
        if (overlay.hasForeground)  { result.foreground = overlay.foreground; result.hasForeground = true; }
        if (overlay.hasBackground)  { result.background = overlay.background; result.hasBackground = true; }
        if (overlay.hasBold)        { result.bold       = overlay.bold;       result.hasBold       = true; }
        if (overlay.hasItalic)      { result.italic     = overlay.italic;     result.hasItalic     = true; }
        if (overlay.hasUnderline)   { result.underline  = overlay.underline;  result.hasUnderline  = true; }
        if (overlay.hasAlignment)   { result.alignment  = overlay.alignment;  result.hasAlignment  = true; }
        if (overlay.hasIcon)        { result.icon       = overlay.icon;       result.hasIcon       = true; }
        return result;
    }
};

#endif // TQTCELLSTYLE_H
