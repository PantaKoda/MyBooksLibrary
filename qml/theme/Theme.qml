pragma Singleton

// The window's shared sizes, spacing and colours (UI overhaul, PR 1). The app
// uses the FluentWinUI3 style (qtquickcontrols2.conf), so these follow
// Fluent's WinUI 3 values, in light and dark as Windows is set. Views use the
// roles here (secondary text, a critical message, a selected row) instead of
// raw colours, opacities and pixel sizes. Colours that the style's palette
// already provides (text, window, accent) come from the palette.
import QtQuick

QtObject {
    // Light or dark: as Windows is set (the style follows it too). Writable
    // only so that a test can check both schemes.
    property int colorScheme: Application.styleHints.colorScheme
    readonly property bool dark: colorScheme === Qt.ColorScheme.Dark

    // Type ramp, Fluent's (Caption 12, Body 14, Body Large 18, Subtitle 20,
    // Title 28 at a 14 px body). A larger application font scales it up; a
    // smaller one (Qt's 9 pt default without the Fluent style) never takes
    // it below Fluent's sizes.
    readonly property real body: Math.max(14, Qt.application.font.pixelSize > 0
                                                  ? Qt.application.font.pixelSize
                                                  : Qt.application.font.pointSize * 4 / 3)
    readonly property int captionSize: Math.round(body * 12 / 14)
    readonly property int bodySize: Math.round(body)
    readonly property int bodyLargeSize: Math.round(body * 18 / 14)
    readonly property int subtitleSize: Math.round(body * 20 / 14)
    readonly property int titleSize: Math.round(body * 28 / 14)
    readonly property int headingWeight: Font.DemiBold

    // Spacing on a 4 px grid, and corner radii (controls; cards and popups).
    readonly property int spacingXS: 4
    readonly property int spacingS: 8
    readonly property int spacingM: 12
    readonly property int spacingL: 16
    readonly property int spacingXL: 24
    readonly property int controlRadius: 4
    readonly property int cardRadius: 8

    // Text that is not the main content: labels, sources, dates, counts.
    // TextFillColorSecondary; at least 4.5:1 on the window in both schemes
    // (tst_theme), so it is never made fainter with opacity.
    readonly property color textSecondary: dark ? Qt.rgba(1, 1, 1, 0.7725) : Qt.rgba(0, 0, 0, 0.62)

    // Status, always together with words (never colour alone). WinUI's
    // status colours; the light caution is darkened from #9d5d00, which falls
    // to 4.4:1 on a selected row.
    readonly property color critical: dark ? "#ff99a4" : "#c42b1c"  // Errors and refusals.
    readonly property color success: dark ? "#6ccb5f" : "#0f7b0f"   // Confirmed, done.
    readonly property color caution: dark ? "#fce100" : "#8a5000"   // Uncertain, needs a look.

    // A selected row the view draws itself (the contents tree), as the style
    // draws one in a list: a subtle fill, the text unchanged, and an accent
    // bar (selectionBarWidth) in the palette's accent colour.
    readonly property color selectedFill: dark ? Qt.rgba(1, 1, 1, 0.0605) : Qt.rgba(0, 0, 0, 0.0373)
    readonly property int selectionBarWidth: 3

    // Cards (a framed group such as the contents entry's details) and dividers.
    readonly property color cardFill: dark ? Qt.rgba(1, 1, 1, 0.0512) : Qt.rgba(1, 1, 1, 0.7)
    readonly property color cardStroke: dark ? Qt.rgba(0, 0, 0, 0.1) : Qt.rgba(0, 0, 0, 0.0578)
    readonly property color divider: dark ? Qt.rgba(1, 1, 1, 0.0837) : Qt.rgba(0, 0, 0, 0.0803)
}
