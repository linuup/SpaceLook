import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQml 2.15

Rectangle {
    id: root
    width: 1024
    height: 720
    color: "#edf3fb"

    property var resolvedUiSettings: fallbackUiSettings
    property int activeNavIndex: 0
    property string displayFontFamily: "Segoe UI Variable Display"
    property string textFontFamily: "Segoe UI Variable Text"
    property string bodyFontFamily: "Segoe UI"
    property string iconFontFamily: iconFontLoader.status === FontLoader.Ready
                                    ? iconFontLoader.name
                                    : "Segoe Fluent Icons"

    property color pageBackgroundTop: "#f7fbff"
    property color pageBackgroundBottom: "#eaf2fb"
    property color shellColor: "#fbfdff"
    property color shellBorderColor: "#dbe6f3"
    property color titleBarColor: "#f9fbfe"
    property color navBarColor: "#f8fbff"
    property color cardColor: "#ffffff"
    property color cardBorderColor: "#d9e4f1"
    property color cardShadowColor: "#17324f14"
    property color accentColor: "#0078d4"
    property color accentSoftColor: "#e7f2fd"
    property color textColor: "#18293d"
    property color secondaryTextColor: "#66788c"
    property color dividerColor: "#e6edf5"
    property color outlineColor: "#c8d5e4"
    property color hoverColor: "#eff5fc"
    property color pressedColor: "#e2ebf7"
    property color destructiveHoverColor: "#d13438"
    property color destructivePressedColor: "#b92f33"

    QtObject {
        id: fallbackUiSettings
        property int menuButtonSize: 38
        property bool showMenuBorder: true
        property int menuPlacement: 0
        property bool showSystemTray: false
        property bool showTaskbar: true
        property bool performanceMode: false
        property bool autoStart: false
        property bool showMenuPin: true
        property bool showMenuOpen: true
        property bool showMenuCopy: true
        property bool showMenuRefresh: true
        property bool showMenuExpand: true
        property bool showMenuClose: true
        property bool showMenuMore: true
    }

    FontLoader {
        id: iconFontLoader
                source: Qt.resolvedUrl("../resources/Segoe Fluent Icons.ttf")
    }

    function navText(index) {
        return index === 0 ? "General" : "Toolbar"
    }

    function navTarget(index) {
        return index === 0 ? generalSection : toolbarSection
    }

    function sectionTargetY(sectionItem) {
        if (!sectionItem) {
            return 0
        }

        var targetY = sectionItem.y - 20
        var maxY = Math.max(0, contentFlickable.contentHeight - contentFlickable.height)
        return Math.max(0, Math.min(targetY, maxY))
    }

    function scrollToSection(index) {
        activeNavIndex = index
        contentScrollAnimation.stop()
        contentScrollAnimation.to = sectionTargetY(navTarget(index))
        contentScrollAnimation.start()
    }

    function syncActiveNavFromScroll() {
        if (contentFlickable.contentY + 48 >= toolbarSection.y) {
            activeNavIndex = 1
            return
        }
        activeNavIndex = 0
    }

    Component.onCompleted: {
        if (typeof uiSettings !== "undefined" && uiSettings) {
            resolvedUiSettings = uiSettings
        }
        syncActiveNavFromScroll()
    }

    component WindowActionButton: Button {
        property string glyph: ""
        property bool destructive: false

        property bool settingsNoDrag: true
        hoverEnabled: true
        padding: 0
        implicitWidth: 42
        implicitHeight: 32
        background: Rectangle {
            radius: 10
            color: parent.down
                   ? (parent.destructive ? root.destructivePressedColor : root.pressedColor)
                   : (parent.hovered ? (parent.destructive ? root.destructiveHoverColor : root.hoverColor) : "transparent")
            border.width: parent.hovered ? 1 : 0
            border.color: parent.hovered
                          ? (parent.destructive ? "#e38f95" : "#d9e4f2")
                          : "transparent"

            Behavior on color {
                ColorAnimation { duration: 120 }
            }
        }

        contentItem: Text {
            text: parent.glyph
            color: parent.destructive && (parent.hovered || parent.down) ? "#ffffff" : root.secondaryTextColor
            font.family: root.iconFontFamily
            font.pixelSize: 16
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            renderType: Text.NativeRendering
        }
    }

    component NavButtonComponent: Button {
        property int navIndex: 0

        property bool settingsNoDrag: true
        hoverEnabled: true
        padding: 0
        implicitWidth: 140
        implicitHeight: 42
        background: Item { }

        contentItem: Text {
            text: root.navText(parent.navIndex)
            color: root.activeNavIndex === parent.navIndex
                   ? root.accentColor
                   : (parent.hovered ? root.textColor : root.secondaryTextColor)
            font.family: root.textFontFamily
            font.pixelSize: root.activeNavIndex === parent.navIndex ? 17 : 15
            font.weight: root.activeNavIndex === parent.navIndex ? Font.DemiBold : Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            renderType: Text.NativeRendering

            Behavior on color {
                ColorAnimation { duration: 140 }
            }
        }

        onClicked: root.scrollToSection(navIndex)
    }

    component CardShell: Rectangle {
        radius: 18
        color: root.cardColor
        border.width: 1
        border.color: root.cardBorderColor

        layer.enabled: true
        layer.samples: 4

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 1
            anchors.bottomMargin: -8
            radius: parent.radius
            color: root.cardShadowColor
            z: -1
        }
    }

    component SectionHeader: RowLayout {
        property string glyph: ""
        property string title: ""
        property string subtitle: ""

        spacing: 12

        Rectangle {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 34
            implicitHeight: 34
            radius: 10
            color: root.accentSoftColor

            Text {
                anchors.centerIn: parent
                text: parent.parent.glyph
                color: root.accentColor
                font.family: root.iconFontFamily
                font.pixelSize: 17
                renderType: Text.NativeRendering
            }
        }

        ColumnLayout {
            spacing: 2

            Text {
                text: parent.parent.title
                color: root.textColor
                font.family: root.textFontFamily
                font.pixelSize: 18
                font.weight: Font.DemiBold
                renderType: Text.NativeRendering
            }

            Text {
                visible: parent.parent.subtitle.length > 0
                text: parent.parent.subtitle
                color: root.secondaryTextColor
                font.family: root.bodyFontFamily
                font.pixelSize: 12
                renderType: Text.NativeRendering
            }
        }
    }

    component MetricPill: Rectangle {
        property string valueText: ""

        radius: 12
        color: root.accentColor
        implicitWidth: 50
        implicitHeight: 24

        Text {
            anchors.centerIn: parent
            text: parent.valueText
            color: "#ffffff"
            font.family: root.textFontFamily
            font.pixelSize: 12
            font.weight: Font.DemiBold
            renderType: Text.NativeRendering
        }
    }

    component DividerLine: Rectangle {
        width: parent ? parent.width : 0
        height: 1
        color: root.dividerColor
    }

    component SettingSwitchRow: Rectangle {
        property string labelText: ""
        property string descriptionText: ""
        property bool checkedValue: false
        signal toggledValue(bool checked)

        width: parent ? parent.width : 0
        height: 72
        radius: 12
        color: rowMouseArea.containsMouse ? "#f8fbff" : "transparent"

        Column {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 2
            anchors.right: rowSwitch.left
            anchors.rightMargin: 16
            spacing: 4

            Text {
                width: parent.width
                text: parent.parent.labelText
                color: root.textColor
                font.family: root.textFontFamily
                font.pixelSize: 16
                font.weight: Font.DemiBold
                renderType: Text.NativeRendering
                elide: Text.ElideRight
            }

            Text {
                width: parent.width
                visible: parent.parent.descriptionText.length > 0
                text: parent.parent.descriptionText
                color: root.secondaryTextColor
                font.family: root.bodyFontFamily
                font.pixelSize: 12
                renderType: Text.NativeRendering
                wrapMode: Text.Wrap
            }
        }

        MouseArea {
            id: rowMouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }

        Switch {
            id: rowSwitch
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 44
            implicitHeight: 24
            hoverEnabled: true
            padding: 0
            checked: parent.checkedValue

            indicator: Rectangle {
                implicitWidth: 44
                implicitHeight: 24
                radius: 12
                color: rowSwitch.checked ? root.accentColor : "#7d8795"
                opacity: rowSwitch.enabled ? 1.0 : 0.45

                Rectangle {
                    width: 16
                    height: 16
                    radius: 8
                    y: 4
                    x: rowSwitch.checked ? 24 : 4
                    color: "#ffffff"

                    Behavior on x {
                        NumberAnimation {
                            duration: 160
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            contentItem: Item { }
            onToggled: parent.toggledValue(checked)
        }
    }

    component ToolbarTile: Rectangle {
        property string titleText: ""
        property string glyph: ""
        property bool checkedValue: false
        signal toggledValue(bool checked)

        Layout.fillWidth: true
        Layout.preferredHeight: 136
        radius: 16
        color: tileMouseArea.containsMouse ? "#f7fbff" : "#fbfdff"
        border.width: 1
        border.color: "#e1eaf4"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Rectangle {
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 42
                implicitHeight: 42
                radius: 12
                color: checkedValue ? root.accentSoftColor : "#f1f6fb"

                Text {
                    anchors.centerIn: parent
                    text: parent.parent.parent.glyph
                    color: checkedValue ? root.accentColor : root.secondaryTextColor
                    font.family: root.iconFontFamily
                    font.pixelSize: 18
                    renderType: Text.NativeRendering
                }
            }

            Text {
                Layout.alignment: Qt.AlignHCenter
                text: parent.parent.titleText
                color: root.textColor
                font.family: root.textFontFamily
                font.pixelSize: 12
                font.weight: Font.Medium
                renderType: Text.NativeRendering
            }

            Item {
                Layout.fillHeight: true
            }

            Switch {
                id: tileSwitch
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: 40
                implicitHeight: 20
                hoverEnabled: true
                padding: 0
                checked: parent.parent.checkedValue
                scale: 0.82

                indicator: Rectangle {
                    implicitWidth: 40
                    implicitHeight: 20
                    radius: 10
                    color: tileSwitch.checked ? root.accentColor : "#7d8795"
                    opacity: tileSwitch.enabled ? 1.0 : 0.45

                    Rectangle {
                        width: 14
                        height: 14
                        radius: 7
                        y: 3
                        x: tileSwitch.checked ? 23 : 3
                        color: "#ffffff"

                        Behavior on x {
                            NumberAnimation {
                                duration: 160
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }

                contentItem: Item { }
                onToggled: parent.parent.toggledValue(checked)
            }
        }

        MouseArea {
            id: tileMouseArea
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.NoButton
        }
    }

    component PlacementOptionButton: Button {
        property int placementValue: 0
        property string labelText: ""

        implicitHeight: 42
        hoverEnabled: true

        background: Rectangle {
            radius: 12
            color: resolvedUiSettings.menuPlacement === parent.placementValue
                   ? root.accentSoftColor
                   : (parent.down ? "#eaf1f8" : (parent.hovered ? "#f5f9fd" : "#ffffff"))
            border.width: 1
            border.color: resolvedUiSettings.menuPlacement === parent.placementValue
                          ? root.accentColor
                          : root.outlineColor
        }

        contentItem: Text {
            text: parent.labelText
            color: resolvedUiSettings.menuPlacement === parent.placementValue ? root.accentColor : root.textColor
            font.family: root.textFontFamily
            font.pixelSize: 13
            font.weight: resolvedUiSettings.menuPlacement === parent.placementValue ? Font.DemiBold : Font.Medium
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            renderType: Text.NativeRendering
        }

        onClicked: resolvedUiSettings.menuPlacement = placementValue
    }

    gradient: Gradient {
        GradientStop { position: 0.0; color: root.pageBackgroundTop }
        GradientStop { position: 1.0; color: root.pageBackgroundBottom }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 16
        color: "#0f1a280a"
        border.width: 1
        border.color: "#0f1a2814"
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 16
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#fcfdff" }
            GradientStop { position: 1.0; color: root.shellColor }
        }
        border.width: 1
        border.color: root.shellBorderColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: root.titleBarColor

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 18
                anchors.rightMargin: 16
                spacing: 12

                RowLayout {
                    spacing: 10

                    Rectangle {
                        width: 14
                        height: 14
                        radius: 4
                        color: root.accentColor
                    }

                    Text {
                        text: "SpaceLook Settings"
                        color: root.textColor
                        font.family: root.textFontFamily
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        renderType: Text.NativeRendering
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                Row {
                    spacing: 4

                    WindowActionButton {
                        glyph: "\uE8BB"
                        destructive: true
                        onClicked: {
                            if (typeof settingsWindow !== "undefined" && settingsWindow) {
                                settingsWindow.requestSettingsWindowClose()
                            }
                        }
                    }
                }
            }

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: root.dividerColor
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 60
            color: root.navBarColor

            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: root.dividerColor
            }

            Item {
                anchors.centerIn: parent
                width: navRow.implicitWidth
                height: parent.height

                Row {
                    id: navRow
                    anchors.centerIn: parent
                    spacing: 14

                    NavButtonComponent {
                        navIndex: 0
                    }

                    NavButtonComponent {
                        navIndex: 1
                    }

                    Rectangle {
                        id: navIndicator
                        y: navRow.height - height
                        height: 4
                        radius: 2
                        color: root.accentColor
                        width: {
                            var button = root.activeNavIndex === 0 ? navRow.children[0] : navRow.children[1]
                            return button ? Math.max(24, button.width * 0.34) : 24
                        }
                        x: {
                            var button = root.activeNavIndex === 0 ? navRow.children[0] : navRow.children[1]
                            return button ? button.x + (button.width - width) / 2 : 0
                        }

                        Behavior on x {
                            NumberAnimation {
                                duration: 220
                                easing.type: Easing.OutCubic
                            }
                        }

                        Behavior on width {
                            NumberAnimation {
                                duration: 180
                                easing.type: Easing.OutCubic
                            }
                        }
                    }
                }
            }
        }

        Flickable {
            id: contentFlickable
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: contentColumn.implicitHeight + 56
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            onContentYChanged: root.syncActiveNavFromScroll()

            NumberAnimation on contentY {
                id: contentScrollAnimation
                duration: 240
                easing.type: Easing.OutCubic
            }

            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                width: 10
                background: Item { }
                contentItem: Rectangle {
                    implicitWidth: 6
                    radius: 3
                    color: "#a7b6c8"
                    opacity: 0.86
                }
            }

            Column {
                id: contentColumn
                width: Math.min(contentFlickable.width - 64, 800)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 30
                spacing: 18

                Column {
                    width: parent.width
                    spacing: 6

                    Text {
                        text: "SpaceLook Settings"
                        color: root.textColor
                        font.family: root.displayFontFamily
                        font.pixelSize: 34
                        font.weight: Font.DemiBold
                        renderType: Text.NativeRendering
                    }

                    Text {
                        width: parent.width
                        text: "Adjust window behavior and choose which preview toolbar actions stay visible."
                        color: root.secondaryTextColor
                        font.family: root.bodyFontFamily
                        font.pixelSize: 14
                        renderType: Text.NativeRendering
                        wrapMode: Text.Wrap
                    }
                }

                CardShell {
                    id: generalSection
                    width: parent.width
                    implicitHeight: 552

                    Column {
                        anchors.fill: parent
                        anchors.margins: 22
                        spacing: 20

                        RowLayout {
                            width: parent.width
                            spacing: 12

                            SectionHeader {
                                glyph: "\uE7C3"
                                title: "General"
                                subtitle: "Global behavior and toolbar sizing."
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            MetricPill {
                                valueText: String(resolvedUiSettings.menuButtonSize)
                            }
                        }

                        Column {
                            width: parent.width
                            spacing: 10

                            Text {
                                text: "Menu button size"
                                color: root.textColor
                                font.family: root.textFontFamily
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                renderType: Text.NativeRendering
                            }

                            Slider {
                                id: menuSizeSlider
                                width: parent.width
                                from: 16
                                to: 64
                                stepSize: 1
                                value: resolvedUiSettings.menuButtonSize
                                onMoved: resolvedUiSettings.menuButtonSize = Math.round(value)
                                onValueChanged: {
                                    if (pressed) {
                                        resolvedUiSettings.menuButtonSize = Math.round(value)
                                    }
                                }

                                background: Rectangle {
                                    x: 0
                                    y: menuSizeSlider.availableHeight / 2 - height / 2
                                    width: menuSizeSlider.availableWidth
                                    height: 4
                                    radius: 2
                                    color: root.outlineColor

                                    Rectangle {
                                        width: menuSizeSlider.visualPosition * parent.width
                                        height: parent.height
                                        radius: 2
                                        color: root.accentColor
                                    }
                                }

                                handle: Rectangle {
                                    x: menuSizeSlider.leftPadding + menuSizeSlider.visualPosition * (menuSizeSlider.availableWidth - width)
                                    y: menuSizeSlider.availableHeight / 2 - height / 2
                                    width: 18
                                    height: 18
                                    radius: 9
                                    color: root.accentColor
                                    border.width: 4
                                    border.color: "#ffffff"
                                }
                            }

                            RowLayout {
                                width: parent.width

                                Text {
                                    text: "Small"
                                    color: root.secondaryTextColor
                                    font.family: root.bodyFontFamily
                                    font.pixelSize: 12
                                    renderType: Text.NativeRendering
                                }

                                Item {
                                    Layout.fillWidth: true
                                }

                                Text {
                                    text: "Large"
                                    color: root.secondaryTextColor
                                    font.family: root.bodyFontFamily
                                    font.pixelSize: 12
                                    renderType: Text.NativeRendering
                                }
                            }
                        }

                        DividerLine { }

                        SectionHeader {
                            glyph: "\uE945"
                            title: "Startup & Behavior"
                            subtitle: "Tray, taskbar, performance, and launch behavior."
                        }

                        Column {
                            width: parent.width
                            spacing: 0

                            SettingSwitchRow {
                                width: parent.width
                                labelText: "Follow system startup"
                                descriptionText: "Launch SpaceLook automatically when Windows starts."
                                checkedValue: resolvedUiSettings.autoStart
                                onToggledValue: resolvedUiSettings.autoStart = checked
                            }

                            DividerLine { }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: "Enable system tray"
                                descriptionText: "Keep a tray entry available for quick access."
                                checkedValue: resolvedUiSettings.showSystemTray
                                onToggledValue: resolvedUiSettings.showSystemTray = checked
                            }

                            DividerLine { }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: "Show in taskbar"
                                descriptionText: "Use a taskbar window instead of a tool window."
                                checkedValue: resolvedUiSettings.showTaskbar
                                onToggledValue: resolvedUiSettings.showTaskbar = checked
                            }

                            DividerLine { }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: "Performance mode"
                                descriptionText: "Reduce heavy visual effects for smoother rendering."
                                checkedValue: resolvedUiSettings.performanceMode
                                onToggledValue: resolvedUiSettings.performanceMode = checked
                            }
                        }
                    }
                }

                CardShell {
                    id: toolbarSection
                    width: parent.width
                    implicitHeight: toolbarSectionContent.implicitHeight + 44

                    Column {
                        id: toolbarSectionContent
                        anchors.fill: parent
                        anchors.margins: 22
                        spacing: 20

                        SectionHeader {
                            glyph: "\uE8FD"
                            title: "Toolbar"
                            subtitle: "Choose which preview actions appear in the title toolbar."
                        }

                        SettingSwitchRow {
                            width: parent.width
                            labelText: "Show menu border"
                            descriptionText: "Display a border around the floating preview menu capsule."
                            checkedValue: resolvedUiSettings.showMenuBorder
                            onToggledValue: resolvedUiSettings.showMenuBorder = checked
                        }

                        DividerLine { }

                        Column {
                            width: parent.width
                            spacing: 10

                            Text {
                                text: "Menu placement"
                                color: root.textColor
                                font.family: root.textFontFamily
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                renderType: Text.NativeRendering
                            }

                            Text {
                                width: parent.width
                                text: "Choose whether the preview menu appears above, below, to the left, or to the right of the preview header content."
                                color: root.secondaryTextColor
                                font.family: root.bodyFontFamily
                                font.pixelSize: 12
                                renderType: Text.NativeRendering
                                wrapMode: Text.Wrap
                            }

                            GridLayout {
                                width: parent.width
                                columns: width >= 520 ? 4 : 2
                                columnSpacing: 10
                                rowSpacing: 10

                                PlacementOptionButton {
                                    Layout.fillWidth: true
                                    placementValue: 0
                                    labelText: "Top"
                                }

                                PlacementOptionButton {
                                    Layout.fillWidth: true
                                    placementValue: 1
                                    labelText: "Bottom"
                                }

                                PlacementOptionButton {
                                    Layout.fillWidth: true
                                    placementValue: 2
                                    labelText: "Left"
                                }

                                PlacementOptionButton {
                                    Layout.fillWidth: true
                                    placementValue: 3
                                    labelText: "Right"
                                }
                            }
                        }

                        DividerLine { }

                        GridLayout {
                            width: parent.width
                            columns: width >= 720 ? 4 : 3
                            columnSpacing: 14
                            rowSpacing: 14

                            ToolbarTile {
                                titleText: "Pin"
                                glyph: "\uE840"
                                checkedValue: resolvedUiSettings.showMenuPin
                                onToggledValue: resolvedUiSettings.showMenuPin = checked
                            }

                            ToolbarTile {
                                titleText: "Open"
                                glyph: "\uE8E5"
                                checkedValue: resolvedUiSettings.showMenuOpen
                                onToggledValue: resolvedUiSettings.showMenuOpen = checked
                            }

                            ToolbarTile {
                                titleText: "Copy"
                                glyph: "\uE8C8"
                                checkedValue: resolvedUiSettings.showMenuCopy
                                onToggledValue: resolvedUiSettings.showMenuCopy = checked
                            }

                            ToolbarTile {
                                titleText: "Refresh"
                                glyph: "\uE72C"
                                checkedValue: resolvedUiSettings.showMenuRefresh
                                onToggledValue: resolvedUiSettings.showMenuRefresh = checked
                            }

                            ToolbarTile {
                                titleText: "Expand"
                                glyph: "\uE740"
                                checkedValue: resolvedUiSettings.showMenuExpand
                                onToggledValue: resolvedUiSettings.showMenuExpand = checked
                            }

                            ToolbarTile {
                                titleText: "Close"
                                glyph: "\uE711"
                                checkedValue: resolvedUiSettings.showMenuClose
                                onToggledValue: resolvedUiSettings.showMenuClose = checked
                            }

                            ToolbarTile {
                                titleText: "More"
                                glyph: "\uE712"
                                checkedValue: resolvedUiSettings.showMenuMore
                                onToggledValue: resolvedUiSettings.showMenuMore = checked
                            }
                        }
                    }
                }

                RowLayout {
                    width: parent.width
                    spacing: 12

                    Item {
                        Layout.fillWidth: true
                    }

                    Button {
                        property bool settingsNoDrag: true
                        text: "Cancel"
                        implicitWidth: 118
                        implicitHeight: 40
                        hoverEnabled: true

                        background: Rectangle {
                            radius: 12
                            color: parent.down ? "#ecf1f7" : (parent.hovered ? "#f6f9fc" : "#ffffff")
                            border.width: 1
                            border.color: root.outlineColor
                        }

                        contentItem: Text {
                            text: parent.text
                            color: root.textColor
                            font.family: root.textFontFamily
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            renderType: Text.NativeRendering
                        }

                        onClicked: {
                            if (typeof settingsWindow !== "undefined" && settingsWindow) {
                                settingsWindow.requestSettingsWindowClose()
                            }
                        }
                    }

                    Button {
                        property bool settingsNoDrag: true
                        text: "Save Changes"
                        implicitWidth: 140
                        implicitHeight: 40
                        hoverEnabled: true

                        background: Rectangle {
                            radius: 12
                            color: parent.down ? "#0069bb" : (parent.hovered ? "#0a83e6" : root.accentColor)
                        }

                        contentItem: Text {
                            text: parent.text
                            color: "#ffffff"
                            font.family: root.textFontFamily
                            font.pixelSize: 14
                            font.weight: Font.Medium
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            renderType: Text.NativeRendering
                        }

                        onClicked: {
                            if (typeof settingsWindow !== "undefined" && settingsWindow) {
                                settingsWindow.requestSettingsWindowClose()
                            }
                        }
                    }
                }

                Item {
                    width: 1
                    height: 28
                }
            }
        }
    }
}
