import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQml 2.15

Rectangle {
    id: root
    width: 1024
    height: 720
    color: "#f5f8fb"

    property var resolvedUiSettings: fallbackUiSettings
    property var resolvedRenderTypeSettings: fallbackRenderTypeSettings
    property int activeNavIndex: 0
    property int selectedRenderTypeIndex: -1
    property var renderTypeEntries: []
    property string displayFontFamily: "Segoe UI Rounded"
    property string textFontFamily: "Segoe UI Rounded"
    property string bodyFontFamily: "Segoe UI Rounded"
    property string iconFontFamily: iconFontLoader.status === FontLoader.Ready
                                    ? iconFontLoader.name
                                    : "Segoe Fluent Icons"

    property color inkColor: "#172331"
    property color mutedInkColor: "#647386"
    property color faintInkColor: "#91a0b2"
    property color shellColor: "#f8fbfd"
    property color sidebarColor: "#edf5f7"
    property color cardColor: "#ffffff"
    property color cardHoverColor: "#f7fbfd"
    property color borderColor: "#d9e5ed"
    property color dividerColor: "#e8eff4"
    property color accentColor: "#0b7f8f"
    property color accentHoverColor: "#0e91a3"
    property color accentSoftColor: "#dff4f6"
    property color warmColor: "#f6efe5"
    property color dangerColor: "#c93a3a"

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

    QtObject {
        id: fallbackRenderTypeSettings
        property string configFilePath: ""
        property string statusMessage: "RenderType settings are unavailable."
        property var entries: []
        signal changed()
        function load() {
        }
        function saveEntries(entries) {
            return false
        }
    }

    FontLoader {
        id: iconFontLoader
        source: Qt.resolvedUrl("../resources/Segoe Fluent Icons.ttf")
    }

    function navText(index) {
        if (index === 0) {
            return "General"
        }
        if (index === 1) {
            return "Toolbar"
        }
        return "File Types"
    }

    function navSubtitle(index) {
        if (index === 0) {
            return "Startup and window behavior"
        }
        if (index === 1) {
            return "Preview menu appearance"
        }
        return "Renderer mapping rules"
    }

    function navGlyph(index) {
        if (index === 0) {
            return "\uE713"
        }
        if (index === 1) {
            return "\uE8FD"
        }
        return "\uE8B7"
    }

    function navTarget(index) {
        if (index === 0) {
            return generalSection
        }
        if (index === 1) {
            return toolbarSection
        }
        return renderTypeSection
    }

    function sectionTargetY(sectionItem) {
        if (!sectionItem) {
            return 0
        }

        var targetY = sectionItem.y - 18
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
        if (contentFlickable.contentY + 80 >= renderTypeSection.y) {
            activeNavIndex = 2
            return
        }
        if (contentFlickable.contentY + 80 >= toolbarSection.y) {
            activeNavIndex = 1
            return
        }

        activeNavIndex = 0
    }

    function copyRenderTypeEntry(entry) {
        return {
            key: entry.key || "",
            name: entry.name || "",
            typeKey: entry.typeKey || "",
            typeDetails: entry.typeDetails || "",
            suffixes: entry.suffixes || ""
        }
    }

    function reloadRenderTypeEntries() {
        if (!resolvedRenderTypeSettings) {
            return
        }
        resolvedRenderTypeSettings.load()
        var loadedEntries = []
        var sourceEntries = resolvedRenderTypeSettings.entries || []
        for (var index = 0; index < sourceEntries.length; ++index) {
            loadedEntries.push(copyRenderTypeEntry(sourceEntries[index]))
        }
        renderTypeEntries = loadedEntries
        selectedRenderTypeIndex = renderTypeEntries.length > 0 ? 0 : -1
        loadSelectedRenderTypeEntry()
    }

    function refreshRenderTypeEntriesFromBridge() {
        if (!resolvedRenderTypeSettings) {
            return
        }
        var loadedEntries = []
        var sourceEntries = resolvedRenderTypeSettings.entries || []
        for (var index = 0; index < sourceEntries.length; ++index) {
            loadedEntries.push(copyRenderTypeEntry(sourceEntries[index]))
        }
        renderTypeEntries = loadedEntries
        if (selectedRenderTypeIndex >= renderTypeEntries.length) {
            selectedRenderTypeIndex = renderTypeEntries.length - 1
        }
        if (selectedRenderTypeIndex < 0 && renderTypeEntries.length > 0) {
            selectedRenderTypeIndex = 0
        }
        loadSelectedRenderTypeEntry()
    }

    function loadSelectedRenderTypeEntry() {
        if (!renderTypeKeyField) {
            return
        }
        if (selectedRenderTypeIndex < 0 || selectedRenderTypeIndex >= renderTypeEntries.length) {
            renderTypeKeyField.text = ""
            renderTypeNameField.text = ""
            renderTypeTypeKeyField.text = ""
            renderTypeDetailsField.text = ""
            renderTypeSuffixesField.text = ""
            return
        }

        var entry = renderTypeEntries[selectedRenderTypeIndex]
        renderTypeKeyField.text = entry.key || ""
        renderTypeNameField.text = entry.name || ""
        renderTypeTypeKeyField.text = entry.typeKey || ""
        renderTypeDetailsField.text = entry.typeDetails || ""
        renderTypeSuffixesField.text = entry.suffixes || ""
    }

    function applyRenderTypeEditor() {
        if (selectedRenderTypeIndex < 0 || selectedRenderTypeIndex >= renderTypeEntries.length) {
            return
        }

        var nextEntries = renderTypeEntries.slice()
        nextEntries[selectedRenderTypeIndex] = {
            key: renderTypeKeyField.text.trim(),
            name: renderTypeNameField.text.trim(),
            typeKey: renderTypeTypeKeyField.text.trim(),
            typeDetails: renderTypeDetailsField.text.trim(),
            suffixes: renderTypeSuffixesField.text.trim()
        }
        renderTypeEntries = nextEntries
    }

    function selectRenderTypeEntry(index) {
        applyRenderTypeEditor()
        selectedRenderTypeIndex = index
        loadSelectedRenderTypeEntry()
    }

    function addRenderTypeEntry() {
        applyRenderTypeEditor()
        var nextEntries = renderTypeEntries.slice()
        nextEntries.push({
            key: "custom",
            name: "SummaryRenderer",
            typeKey: "custom",
            typeDetails: "Custom file type.",
            suffixes: "example"
        })
        renderTypeEntries = nextEntries
        selectedRenderTypeIndex = renderTypeEntries.length - 1
        loadSelectedRenderTypeEntry()
    }

    function removeRenderTypeEntry() {
        if (selectedRenderTypeIndex < 0 || selectedRenderTypeIndex >= renderTypeEntries.length) {
            return
        }
        var nextEntries = renderTypeEntries.slice()
        nextEntries.splice(selectedRenderTypeIndex, 1)
        renderTypeEntries = nextEntries
        selectedRenderTypeIndex = Math.min(selectedRenderTypeIndex, renderTypeEntries.length - 1)
        loadSelectedRenderTypeEntry()
    }

    function saveRenderTypeEntries() {
        applyRenderTypeEditor()
        if (resolvedRenderTypeSettings) {
            resolvedRenderTypeSettings.saveEntries(renderTypeEntries)
        }
    }

    Component.onCompleted: {
        if (typeof uiSettings !== "undefined" && uiSettings) {
            resolvedUiSettings = uiSettings
        }
        if (typeof renderTypeSettings !== "undefined" && renderTypeSettings) {
            resolvedRenderTypeSettings = renderTypeSettings
        }
        reloadRenderTypeEntries()
        syncActiveNavFromScroll()
    }

    Connections {
        target: root.resolvedRenderTypeSettings
        function onChanged() {
            root.refreshRenderTypeEntriesFromBridge()
        }
    }

    component WindowActionButton: Button {
        property string glyph: ""

        property bool settingsNoDrag: true
        hoverEnabled: true
        padding: 0
        implicitWidth: 38
        implicitHeight: 30
        background: Rectangle {
            radius: 9
            color: parent.down ? "#b92f33" : (parent.hovered ? root.dangerColor : "transparent")
            border.width: parent.hovered ? 1 : 0
            border.color: parent.hovered ? "#d77070" : "transparent"

            Behavior on color {
                ColorAnimation {
                    duration: 120
                }
            }
        }

        contentItem: Text {
            text: parent.glyph
            color: parent.hovered || parent.down ? "#ffffff" : root.mutedInkColor
            font.family: root.iconFontFamily
            font.pixelSize: 15
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            renderType: Text.NativeRendering
        }
    }

    component SidebarButton: Button {
        id: sidebarButton
        property int navIndex: 0

        property bool settingsNoDrag: true
        hoverEnabled: true
        padding: 0
        implicitHeight: 68

        background: Rectangle {
            radius: 18
            color: root.activeNavIndex === sidebarButton.navIndex
                   ? "#ffffff"
                   : (parent.hovered ? "#f7fbfd" : "transparent")
            border.width: root.activeNavIndex === sidebarButton.navIndex ? 1 : 0
            border.color: root.borderColor

            Rectangle {
                visible: root.activeNavIndex === sidebarButton.navIndex
                anchors.left: parent.left
                anchors.leftMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                width: 4
                height: 28
                radius: 2
                color: root.accentColor
            }
        }

        contentItem: RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 22
            anchors.rightMargin: 14
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 34
                Layout.preferredHeight: 34
                radius: 11
                color: root.activeNavIndex === sidebarButton.navIndex ? root.accentSoftColor : "#ffffff"
                border.width: 1
                border.color: root.activeNavIndex === sidebarButton.navIndex ? "#b6dde2" : "#e5edf3"

                Text {
                    anchors.centerIn: parent
                    text: root.navGlyph(sidebarButton.navIndex)
                    color: root.activeNavIndex === sidebarButton.navIndex ? root.accentColor : root.mutedInkColor
                    font.family: root.iconFontFamily
                    font.pixelSize: 16
                    renderType: Text.NativeRendering
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Text {
                    Layout.fillWidth: true
                    text: root.navText(sidebarButton.navIndex)
                    color: root.inkColor
                    font.family: root.textFontFamily
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }

                Text {
                    Layout.fillWidth: true
                    text: root.navSubtitle(sidebarButton.navIndex)
                    color: root.mutedInkColor
                    font.family: root.bodyFontFamily
                    font.pixelSize: 11
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }
            }
        }

        onClicked: root.scrollToSection(navIndex)
    }

    component CardShell: Rectangle {
        radius: 22
        color: root.cardColor
        border.width: 1
        border.color: root.borderColor

        Rectangle {
            anchors.fill: parent
            anchors.topMargin: 2
            anchors.bottomMargin: -7
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            radius: parent.radius
            color: "#12344a10"
            z: -1
        }
    }

    component SectionHeader: RowLayout {
        id: sectionHeader
        property string glyph: ""
        property string title: ""
        property string subtitle: ""

        spacing: 14

        Rectangle {
            Layout.preferredWidth: 42
            Layout.preferredHeight: 42
            radius: 14
            gradient: Gradient {
                GradientStop {
                    position: 0.0
                    color: root.accentSoftColor
                }
                GradientStop {
                    position: 1.0
                    color: root.warmColor
                }
            }

            Text {
                anchors.centerIn: parent
                text: sectionHeader.glyph
                color: root.accentColor
                font.family: root.iconFontFamily
                font.pixelSize: 18
                renderType: Text.NativeRendering
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 3

            Text {
                Layout.fillWidth: true
                text: sectionHeader.title
                color: root.inkColor
                font.family: root.displayFontFamily
                font.pixelSize: 22
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                Layout.fillWidth: true
                text: sectionHeader.subtitle
                color: root.mutedInkColor
                font.family: root.bodyFontFamily
                font.pixelSize: 12
                wrapMode: Text.Wrap
                renderType: Text.NativeRendering
            }
        }
    }

    component DividerLine: Rectangle {
        width: parent ? parent.width : 0
        height: 1
        color: root.dividerColor
    }

    component SettingSwitchRow: Rectangle {
        id: settingSwitchRow
        property string labelText: ""
        property string descriptionText: ""
        property bool checkedValue: false
        signal toggledValue(bool checked)

        width: parent ? parent.width : 0
        height: 70
        radius: 16
        color: rowMouseArea.containsMouse ? root.cardHoverColor : "transparent"

        Column {
            anchors.left: parent.left
            anchors.right: rowSwitch.left
            anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            spacing: 4

            Text {
                width: parent.width
                text: settingSwitchRow.labelText
                color: root.inkColor
                font.family: root.textFontFamily
                font.pixelSize: 15
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                width: parent.width
                text: settingSwitchRow.descriptionText
                color: root.mutedInkColor
                font.family: root.bodyFontFamily
                font.pixelSize: 12
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                renderType: Text.NativeRendering
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
            property bool settingsNoDrag: true
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: 48
            implicitHeight: 26
            hoverEnabled: true
            padding: 0
            checked: settingSwitchRow.checkedValue

            indicator: Rectangle {
                implicitWidth: 48
                implicitHeight: 26
                radius: 13
                color: rowSwitch.checked ? root.accentColor : "#a7b2bd"

                Rectangle {
                    width: 20
                    height: 20
                    radius: 10
                    y: 3
                    x: rowSwitch.checked ? 25 : 3
                    color: "#ffffff"

                    Behavior on x {
                        NumberAnimation {
                            duration: 170
                            easing.type: Easing.OutCubic
                        }
                    }
                }
            }

            contentItem: Item { }
            onToggled: settingSwitchRow.toggledValue(checked)
        }
    }

    component ToolbarTile: Rectangle {
        id: toolbarTile
        property string titleText: ""
        property string glyph: ""
        property bool checkedValue: false
        signal toggledValue(bool checked)

        Layout.fillWidth: true
        Layout.preferredHeight: 82
        radius: 18
        color: tileMouseArea.containsMouse ? root.cardHoverColor : "#fbfdfe"
        border.width: 1
        border.color: toolbarTile.checkedValue ? "#a9d5db" : "#e1eaf0"

        RowLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 12

            Rectangle {
                Layout.preferredWidth: 38
                Layout.preferredHeight: 38
                radius: 13
                color: toolbarTile.checkedValue ? root.accentSoftColor : "#f0f5f7"

                Text {
                    anchors.centerIn: parent
                    text: toolbarTile.glyph
                    color: toolbarTile.checkedValue ? root.accentColor : root.mutedInkColor
                    font.family: root.iconFontFamily
                    font.pixelSize: 17
                    renderType: Text.NativeRendering
                }
            }

            Text {
                Layout.fillWidth: true
                text: toolbarTile.titleText
                color: root.inkColor
                font.family: root.textFontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Switch {
                id: tileSwitch
                property bool settingsNoDrag: true
                implicitWidth: 40
                implicitHeight: 22
                hoverEnabled: true
                padding: 0
                checked: toolbarTile.checkedValue

                indicator: Rectangle {
                    implicitWidth: 40
                    implicitHeight: 22
                    radius: 11
                    color: tileSwitch.checked ? root.accentColor : "#a7b2bd"

                    Rectangle {
                        width: 16
                        height: 16
                        radius: 8
                        y: 3
                        x: tileSwitch.checked ? 21 : 3
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
                onToggled: toolbarTile.toggledValue(checked)
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
        id: placementOptionButton
        property int placementValue: 0
        property string labelText: ""
        property string glyph: ""

        property bool settingsNoDrag: true
        implicitHeight: 46
        hoverEnabled: true
        padding: 0

        background: Rectangle {
            radius: 16
            color: resolvedUiSettings.menuPlacement === placementOptionButton.placementValue
                   ? root.accentSoftColor
                   : (parent.down ? "#edf4f6" : (parent.hovered ? root.cardHoverColor : "#ffffff"))
            border.width: 1
            border.color: resolvedUiSettings.menuPlacement === placementOptionButton.placementValue
                          ? root.accentColor
                          : root.borderColor
        }

        contentItem: RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8

            Text {
                text: placementOptionButton.glyph
                color: resolvedUiSettings.menuPlacement === placementOptionButton.placementValue ? root.accentColor : root.mutedInkColor
                font.family: root.iconFontFamily
                font.pixelSize: 15
                renderType: Text.NativeRendering
            }

            Text {
                Layout.fillWidth: true
                text: placementOptionButton.labelText
                color: resolvedUiSettings.menuPlacement === placementOptionButton.placementValue ? root.accentColor : root.inkColor
                font.family: root.textFontFamily
                font.pixelSize: 13
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                renderType: Text.NativeRendering
            }
        }

        onClicked: resolvedUiSettings.menuPlacement = placementValue
    }

    gradient: Gradient {
        GradientStop {
            position: 0.0
            color: "#f2f7f8"
        }
        GradientStop {
            position: 0.52
            color: "#fbf7ef"
        }
        GradientStop {
            position: 1.0
            color: "#eaf3f6"
        }
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 1
        radius: 20
        color: "#0e25310b"
        border.width: 1
        border.color: "#16323f18"
    }

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        radius: 20
        color: root.shellColor
        border.width: 1
        border.color: root.borderColor
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            color: "transparent"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 16
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: 30
                    radius: 10
                    color: "#ffffff"
                    border.width: 1
                    border.color: root.borderColor

                    Image {
                        anchors.fill: parent
                        anchors.margins: 4
                        source: "qrc:/icons/icon.png"
                        fillMode: Image.PreserveAspectFit
                        smooth: true
                        mipmap: true
                    }
                }

                ColumnLayout {
                    spacing: 0

                    Text {
                        text: "Settings"
                        color: root.inkColor
                        font.family: root.displayFontFamily
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                        renderType: Text.NativeRendering
                    }

                    Text {
                        text: "SpaceLook preview preferences"
                        color: root.mutedInkColor
                        font.family: root.bodyFontFamily
                        font.pixelSize: 11
                        renderType: Text.NativeRendering
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                WindowActionButton {
                    glyph: "\uE8BB"
                    onClicked: {
                        if (typeof settingsWindow !== "undefined" && settingsWindow) {
                            settingsWindow.requestSettingsWindowClose()
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 14
            Layout.rightMargin: 14
            Layout.bottomMargin: 14
            spacing: 14

            Rectangle {
                Layout.preferredWidth: 218
                Layout.fillHeight: true
                radius: 24
                color: root.sidebarColor
                border.width: 1
                border.color: "#d5e3e8"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14

                    SidebarButton {
                        Layout.fillWidth: true
                        navIndex: 0
                    }

                    SidebarButton {
                        Layout.fillWidth: true
                        navIndex: 1
                    }

                    SidebarButton {
                        Layout.fillWidth: true
                        navIndex: 2
                    }

                    Item {
                        Layout.fillHeight: true
                    }
                }
            }

            Flickable {
                id: contentFlickable
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: contentColumn.implicitHeight + 28
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
                        implicitWidth: 5
                        radius: 3
                        color: "#9aabb5"
                        opacity: 0.74
                    }
                }

                Column {
                    id: contentColumn
                    width: contentFlickable.width - 10
                    anchors.top: parent.top
                    spacing: 16

                    CardShell {
                        id: generalSection
                        width: parent.width
                        implicitHeight: generalContent.implicitHeight + 42

                        Column {
                            id: generalContent
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 18

                            SectionHeader {
                                width: parent.width
                                glyph: "\uE713"
                                title: "General"
                                subtitle: "Control startup behavior, tray presence, taskbar visibility, and preview menu size."
                            }

                            Rectangle {
                                width: parent.width
                                height: 112
                                radius: 18
                                color: "#f8fbfc"
                                border.width: 1
                                border.color: "#e1ebf0"

                                Column {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 12

                                    RowLayout {
                                        width: parent.width

                                        Text {
                                            text: "Menu button size"
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                            renderType: Text.NativeRendering
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }
                                    }

                                    Slider {
                                        id: menuSizeSlider
                                        width: parent.width
                                        from: 30
                                        to: 72
                                        stepSize: 1
                                        value: resolvedUiSettings.menuButtonSize
                                        leftPadding: 10
                                        rightPadding: 10

                                        onMoved: resolvedUiSettings.menuButtonSize = Math.round(value)
                                        onValueChanged: {
                                            if (pressed) {
                                                resolvedUiSettings.menuButtonSize = Math.round(value)
                                            }
                                        }

                                        background: Rectangle {
                                            x: menuSizeSlider.leftPadding
                                            y: menuSizeSlider.availableHeight / 2 - height / 2
                                            width: menuSizeSlider.availableWidth
                                            height: 5
                                            radius: 3
                                            color: "#d5e1e7"

                                            Rectangle {
                                                width: menuSizeSlider.visualPosition * parent.width
                                                height: parent.height
                                                radius: 3
                                                color: root.accentColor
                                            }
                                        }

                                        handle: Rectangle {
                                            x: menuSizeSlider.leftPadding + menuSizeSlider.visualPosition * (menuSizeSlider.availableWidth - width)
                                            y: menuSizeSlider.availableHeight / 2 - height / 2
                                            width: 20
                                            height: 20
                                            radius: 10
                                            color: "#ffffff"
                                            border.width: 5
                                            border.color: root.accentColor
                                        }
                                    }

                                    RowLayout {
                                        width: parent.width

                                        Text {
                                            text: "Small"
                                            color: root.faintInkColor
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 11
                                            renderType: Text.NativeRendering
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: "Large"
                                            color: root.faintInkColor
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 11
                                            renderType: Text.NativeRendering
                                        }
                                    }
                                }
                            }

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
                                labelText: "Show preview in taskbar"
                                descriptionText: "Only affects the preview window. The Settings window stays independent."
                                checkedValue: resolvedUiSettings.showTaskbar
                                onToggledValue: resolvedUiSettings.showTaskbar = checked
                            }

                            DividerLine { }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: "Performance mode"
                                descriptionText: "Use the QML preview path when available for smoother rendering."
                                checkedValue: resolvedUiSettings.performanceMode
                                onToggledValue: resolvedUiSettings.performanceMode = checked
                            }
                        }
                    }

                    CardShell {
                        id: toolbarSection
                        width: parent.width
                        implicitHeight: toolbarContent.implicitHeight + 42

                        Column {
                            id: toolbarContent
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 18

                            SectionHeader {
                                width: parent.width
                                glyph: "\uE8FD"
                                title: "Toolbar"
                                subtitle: "Tune the capsule menu, placement, border, and visible actions."
                            }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: "Show menu border"
                                descriptionText: "Draw a tight border around the preview capsule menu."
                                checkedValue: resolvedUiSettings.showMenuBorder
                                onToggledValue: resolvedUiSettings.showMenuBorder = checked
                            }

                            DividerLine { }

                            Column {
                                width: parent.width
                                spacing: 12

                                Text {
                                    text: "Menu placement"
                                    color: root.inkColor
                                    font.family: root.textFontFamily
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    renderType: Text.NativeRendering
                                }

                                GridLayout {
                                    width: parent.width
                                    columns: width >= 520 ? 4 : 2
                                    columnSpacing: 10
                                    rowSpacing: 10

                                    PlacementOptionButton {
                                        Layout.fillWidth: true
                                        placementValue: 0
                                        glyph: "\uE70E"
                                        labelText: "Top"
                                    }

                                    PlacementOptionButton {
                                        Layout.fillWidth: true
                                        placementValue: 1
                                        glyph: "\uE70D"
                                        labelText: "Bottom"
                                    }

                                    PlacementOptionButton {
                                        Layout.fillWidth: true
                                        placementValue: 2
                                        glyph: "\uE76B"
                                        labelText: "Left"
                                    }

                                    PlacementOptionButton {
                                        Layout.fillWidth: true
                                        placementValue: 3
                                        glyph: "\uE76C"
                                        labelText: "Right"
                                    }
                                }
                            }

                            DividerLine { }

                            Text {
                                text: "Visible actions"
                                color: root.inkColor
                                font.family: root.textFontFamily
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                                renderType: Text.NativeRendering
                            }

                            GridLayout {
                                width: parent.width
                                columns: width >= 620 ? 3 : 2
                                columnSpacing: 12
                                rowSpacing: 12

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

                    CardShell {
                        id: renderTypeSection
                        width: parent.width
                        implicitHeight: renderTypeContent.implicitHeight + 42

                        Column {
                            id: renderTypeContent
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 18

                            SectionHeader {
                                width: parent.width
                                glyph: "\uE8B7"
                                title: "File Types"
                                subtitle: "Edit RenderType.json mappings. Each row maps suffixes to one renderer."
                            }

                            Rectangle {
                                width: parent.width
                                height: 42
                                radius: 14
                                color: "#f8fbfc"
                                border.width: 1
                                border.color: "#e1ebf0"

                                Text {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 14
                                    anchors.rightMargin: 14
                                    text: resolvedRenderTypeSettings.configFilePath
                                    color: root.mutedInkColor
                                    font.family: root.bodyFontFamily
                                    font.pixelSize: 11
                                    elide: Text.ElideMiddle
                                    renderType: Text.NativeRendering
                                }
                            }

                            RowLayout {
                                width: parent.width
                                height: 430
                                spacing: 14

                                Rectangle {
                                    Layout.preferredWidth: 260
                                    Layout.fillHeight: true
                                    radius: 18
                                    color: "#f8fbfc"
                                    border.width: 1
                                    border.color: "#e1ebf0"

                                    ListView {
                                        id: renderTypeList
                                        property bool settingsNoDrag: true
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        clip: true
                                        spacing: 6
                                        model: root.renderTypeEntries

                                        delegate: Rectangle {
                                            property bool settingsNoDrag: true
                                            width: renderTypeList.width
                                            height: 58
                                            radius: 14
                                            color: index === root.selectedRenderTypeIndex ? root.accentSoftColor : (rowMouse.containsMouse ? "#ffffff" : "transparent")
                                            border.width: index === root.selectedRenderTypeIndex ? 1 : 0
                                            border.color: "#b6dde2"

                                            Column {
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                anchors.verticalCenter: parent.verticalCenter
                                                anchors.leftMargin: 12
                                                anchors.rightMargin: 12
                                                spacing: 2

                                                Text {
                                                    width: parent.width
                                                    text: modelData.key || "Untitled"
                                                    color: root.inkColor
                                                    font.family: root.textFontFamily
                                                    font.pixelSize: 13
                                                    font.weight: Font.DemiBold
                                                    elide: Text.ElideRight
                                                    renderType: Text.NativeRendering
                                                }

                                                Text {
                                                    width: parent.width
                                                    text: (modelData.name || "") + "  " + (modelData.suffixes || "")
                                                    color: root.mutedInkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                    renderType: Text.NativeRendering
                                                }
                                            }

                                            MouseArea {
                                                id: rowMouse
                                                anchors.fill: parent
                                                hoverEnabled: true
                                                onClicked: root.selectRenderTypeEntry(index)
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    radius: 18
                                    color: "#fbfdfe"
                                    border.width: 1
                                    border.color: "#e1ebf0"

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 12

                                        Text {
                                            text: "Mapping details"
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 15
                                            font.weight: Font.DemiBold
                                            renderType: Text.NativeRendering
                                        }

                                        GridLayout {
                                            width: parent.width
                                            columns: 2
                                            columnSpacing: 12
                                            rowSpacing: 10

                                            TextField {
                                                id: renderTypeKeyField
                                                property bool settingsNoDrag: true
                                                Layout.fillWidth: true
                                                placeholderText: "Group key, for example code"
                                                font.family: root.bodyFontFamily
                                                font.pixelSize: 13
                                                onEditingFinished: root.applyRenderTypeEditor()
                                            }

                                            TextField {
                                                id: renderTypeNameField
                                                property bool settingsNoDrag: true
                                                Layout.fillWidth: true
                                                placeholderText: "Renderer, for example CodeRenderer"
                                                font.family: root.bodyFontFamily
                                                font.pixelSize: 13
                                                onEditingFinished: root.applyRenderTypeEditor()
                                            }

                                            TextField {
                                                id: renderTypeTypeKeyField
                                                property bool settingsNoDrag: true
                                                Layout.fillWidth: true
                                                placeholderText: "typeKey, for example code"
                                                font.family: root.bodyFontFamily
                                                font.pixelSize: 13
                                                onEditingFinished: root.applyRenderTypeEditor()
                                            }

                                            TextField {
                                                id: renderTypeDetailsField
                                                property bool settingsNoDrag: true
                                                Layout.fillWidth: true
                                                placeholderText: "Type description"
                                                font.family: root.bodyFontFamily
                                                font.pixelSize: 13
                                                onEditingFinished: root.applyRenderTypeEditor()
                                            }
                                        }

                                        Text {
                                            text: "Suffixes"
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                            renderType: Text.NativeRendering
                                        }

                                        TextArea {
                                            id: renderTypeSuffixesField
                                            property bool settingsNoDrag: true
                                            width: parent.width
                                            height: 120
                                            wrapMode: TextEdit.Wrap
                                            placeholderText: "Comma or space separated suffixes, for example ts tsx js"
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 13
                                            selectByMouse: true
                                            background: Rectangle {
                                                radius: 14
                                                color: "#ffffff"
                                                border.width: 1
                                                border.color: root.borderColor
                                            }
                                        }

                                        Rectangle {
                                            width: parent.width
                                            height: 58
                                            radius: 16
                                            color: "#f8fbfc"
                                            border.width: 1
                                            border.color: "#e1ebf0"

                                            Text {
                                                anchors.fill: parent
                                                anchors.margins: 12
                                                text: resolvedRenderTypeSettings.statusMessage
                                                color: root.mutedInkColor
                                                font.family: root.bodyFontFamily
                                                font.pixelSize: 12
                                                wrapMode: Text.Wrap
                                                renderType: Text.NativeRendering
                                            }
                                        }

                                        RowLayout {
                                            width: parent.width
                                            spacing: 10

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: "Add"
                                                onClicked: root.addRenderTypeEntry()
                                            }

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: "Remove"
                                                enabled: root.selectedRenderTypeIndex >= 0
                                                onClicked: root.removeRenderTypeEntry()
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                            }

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: "Reload"
                                                onClicked: root.reloadRenderTypeEntries()
                                            }

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: "Save"
                                                highlighted: true
                                                onClicked: root.saveRenderTypeEntries()
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Item {
                        width: 1
                        height: 20
                    }
                }
            }
        }
    }
}
