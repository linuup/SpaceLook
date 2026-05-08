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
    property var filteredRenderTypeEntries: []
    property string renderTypeFilter: ""
    property bool loadingRenderTypeEditor: false
    property bool wideLayout: width >= 860
    property bool roomyFileTypeLayout: width >= 980
    property int pageMargin: width >= 760 ? 14 : 8
    property string displayFontFamily: "Segoe UI Variable"
    property string textFontFamily: "Segoe UI"
    property string bodyFontFamily: "Segoe UI"
    property int displayFontWeight: Font.DemiBold
    property int labelFontWeight: Font.Medium
    property int bodyFontWeight: Font.Normal
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
        property bool showMenuOcr: true
        property bool showMenuRefresh: true
        property bool showMenuExpand: true
        property bool showMenuClose: true
        property bool showMenuMore: true
        property string language: "en"
        property string ocrEngine: "windows"
        property string baiduOcrApiKey: ""
        property string baiduOcrSecretKey: ""
        property bool baiduOcrCredentialTestBusy: false
        property string baiduOcrCredentialTestMessage: ""
        function testBaiduOcrCredentials() {
            baiduOcrCredentialTestMessage = qsTr("Baidu credential test is unavailable.")
        }
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
            return qsTr("General")
        }
        if (index === 1) {
            return qsTr("Toolbar")
        }
        if (index === 2) {
            return qsTr("OCR")
        }
        return qsTr("File Types")
    }

    function navSubtitle(index) {
        if (index === 0) {
            return qsTr("Startup and window behavior")
        }
        if (index === 1) {
            return qsTr("Preview menu appearance")
        }
        if (index === 2) {
            return qsTr("Text recognition model")
        }
        return qsTr("Renderer mapping rules")
    }

    function navGlyph(index) {
        if (index === 0) {
            return "\uE713"
        }
        if (index === 1) {
            return "\uE8FD"
        }
        if (index === 2) {
            return "\uE8C8"
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
        if (index === 2) {
            return ocrSection
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
            activeNavIndex = 3
            return
        }
        if (contentFlickable.contentY + 80 >= ocrSection.y) {
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

    function suffixArray(text) {
        var parts = (text || "").split(/[\s,;]+/)
        var output = []
        var seen = {}
        for (var index = 0; index < parts.length; ++index) {
            var suffix = normalizeSuffixToken(parts[index])
            if (suffix.length > 0 && !seen[suffix]) {
                seen[suffix] = true
                output.push(suffix)
            }
        }
        return output
    }

    function normalizeSuffixToken(text) {
        var suffix = (text || "").trim().toLowerCase()
        while (suffix.charAt(0) === ".") {
            suffix = suffix.substring(1)
        }
        return suffix
    }

    function normalizeSuffixText(text) {
        return suffixArray(text).join(", ")
    }

    function suffixCountText(text) {
        var count = suffixArray(text).length
        return count === 1 ? "1 suffix" : count + " suffixes"
    }

    function renderTypeMatchesFilter(entry) {
        var filter = renderTypeFilter.trim().toLowerCase()
        if (filter.length === 0) {
            return true
        }
        var haystack = ((entry.key || "") + " " + (entry.name || "") + " " + (entry.typeKey || "") + " " + (entry.typeDetails || "") + " " + (entry.suffixes || "")).toLowerCase()
        return haystack.indexOf(filter) >= 0
    }

    function rebuildFilteredRenderTypeEntries() {
        var visibleEntries = []
        for (var index = 0; index < renderTypeEntries.length; ++index) {
            if (renderTypeMatchesFilter(renderTypeEntries[index])) {
                var copied = copyRenderTypeEntry(renderTypeEntries[index])
                copied.sourceIndex = index
                visibleEntries.push(copied)
            }
        }
        filteredRenderTypeEntries = visibleEntries
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
        rebuildFilteredRenderTypeEntries()
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
        rebuildFilteredRenderTypeEntries()
        loadSelectedRenderTypeEntry()
    }

    function loadSelectedRenderTypeEntry() {
        if (!renderTypeKeyField) {
            return
        }
        loadingRenderTypeEditor = true
        if (selectedRenderTypeIndex < 0 || selectedRenderTypeIndex >= renderTypeEntries.length) {
            renderTypeKeyField.text = ""
            renderTypeNameField.text = ""
            renderTypeTypeKeyField.text = ""
            renderTypeDetailsField.text = ""
            renderTypeSuffixesField.text = ""
            loadingRenderTypeEditor = false
            return
        }

        var entry = renderTypeEntries[selectedRenderTypeIndex]
        renderTypeKeyField.text = entry.key || ""
        renderTypeNameField.text = entry.name || ""
        renderTypeTypeKeyField.text = entry.typeKey || ""
        renderTypeDetailsField.text = entry.typeDetails || ""
        renderTypeSuffixesField.text = entry.suffixes || ""
        loadingRenderTypeEditor = false
    }

    function applyRenderTypeEditor() {
        if (loadingRenderTypeEditor) {
            return
        }
        if (selectedRenderTypeIndex < 0 || selectedRenderTypeIndex >= renderTypeEntries.length) {
            return
        }

        var nextEntries = renderTypeEntries.slice()
        nextEntries[selectedRenderTypeIndex] = {
            key: renderTypeKeyField.text.trim(),
            name: renderTypeNameField.text.trim(),
            typeKey: renderTypeTypeKeyField.text.trim(),
            typeDetails: renderTypeDetailsField.text.trim(),
            suffixes: normalizeSuffixText(renderTypeSuffixesField.text)
        }
        renderTypeEntries = nextEntries
        rebuildFilteredRenderTypeEntries()
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
        rebuildFilteredRenderTypeEntries()
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
        rebuildFilteredRenderTypeEntries()
        loadSelectedRenderTypeEntry()
    }

    function addRenderTypeSuffix(text) {
        if (!renderTypeSuffixesField) {
            return
        }
        var suffix = normalizeSuffixToken(text)
        if (suffix.length === 0) {
            return
        }
        var values = suffixArray(renderTypeSuffixesField.text)
        for (var index = 0; index < values.length; ++index) {
            if (values[index] === suffix) {
                return
            }
        }
        values.push(suffix)
        renderTypeSuffixesField.text = values.join(", ")
        applyRenderTypeEditor()
    }

    function removeRenderTypeSuffixAt(index) {
        if (!renderTypeSuffixesField) {
            return
        }
        var values = suffixArray(renderTypeSuffixesField.text)
        if (index < 0 || index >= values.length) {
            return
        }
        values.splice(index, 1)
        renderTypeSuffixesField.text = values.join(", ")
        applyRenderTypeEditor()
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
                    font.weight: root.labelFontWeight
                    elide: Text.ElideRight
                    renderType: Text.NativeRendering
                }

                Text {
                    Layout.fillWidth: true
                    text: root.navSubtitle(sidebarButton.navIndex)
                    color: root.mutedInkColor
                    font.family: root.bodyFontFamily
                    font.pixelSize: 11
                    font.weight: root.bodyFontWeight
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
                font.weight: root.displayFontWeight
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                Layout.fillWidth: true
                text: sectionHeader.subtitle
                color: root.mutedInkColor
                font.family: root.bodyFontFamily
                font.pixelSize: 12
                font.weight: root.bodyFontWeight
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
                font.weight: root.labelFontWeight
                elide: Text.ElideRight
                renderType: Text.NativeRendering
            }

            Text {
                width: parent.width
                text: settingSwitchRow.descriptionText
                color: root.mutedInkColor
                font.family: root.bodyFontFamily
                font.pixelSize: 12
                font.weight: root.bodyFontWeight
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
                font.weight: root.labelFontWeight
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
                font.weight: root.labelFontWeight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                renderType: Text.NativeRendering
            }
        }

        onClicked: resolvedUiSettings.menuPlacement = placementValue
    }

    component OcrEngineOptionButton: Button {
        id: ocrEngineOptionButton
        property string engineValue: "windows"
        property string labelText: ""
        property string glyph: ""

        property bool settingsNoDrag: true
        implicitHeight: 46
        hoverEnabled: true
        padding: 0

        background: Rectangle {
            radius: 16
            color: resolvedUiSettings.ocrEngine === ocrEngineOptionButton.engineValue
                   ? root.accentSoftColor
                   : (parent.down ? "#edf4f6" : (parent.hovered ? root.cardHoverColor : "#ffffff"))
            border.width: 1
            border.color: resolvedUiSettings.ocrEngine === ocrEngineOptionButton.engineValue
                          ? root.accentColor
                          : root.borderColor
        }

        contentItem: RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 8

            Text {
                text: ocrEngineOptionButton.glyph
                color: resolvedUiSettings.ocrEngine === ocrEngineOptionButton.engineValue ? root.accentColor : root.mutedInkColor
                font.family: root.iconFontFamily
                font.pixelSize: 15
                renderType: Text.NativeRendering
            }

            Text {
                Layout.fillWidth: true
                text: ocrEngineOptionButton.labelText
                color: resolvedUiSettings.ocrEngine === ocrEngineOptionButton.engineValue ? root.accentColor : root.inkColor
                font.family: root.textFontFamily
                font.pixelSize: 13
                font.weight: root.labelFontWeight
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                renderType: Text.NativeRendering
            }
        }

        onClicked: resolvedUiSettings.ocrEngine = engineValue
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
                        text: qsTr("Settings")
                        color: root.inkColor
                        font.family: root.displayFontFamily
                        font.pixelSize: 17
                        font.weight: root.displayFontWeight
                        renderType: Text.NativeRendering
                    }

                    Text {
                        text: qsTr("SpaceLook preview preferences")
                        color: root.mutedInkColor
                        font.family: root.bodyFontFamily
                        font.pixelSize: 11
                        font.weight: root.bodyFontWeight
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

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: root.pageMargin
            Layout.rightMargin: root.pageMargin
            Layout.bottomMargin: root.pageMargin
            spacing: root.pageMargin

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: root.wideLayout ? 0 : 76
                visible: !root.wideLayout
                radius: 22
                color: root.sidebarColor
                border.width: 1
                border.color: "#d5e3e8"

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10

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

                    SidebarButton {
                        Layout.fillWidth: true
                        navIndex: 3
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: root.pageMargin

                Rectangle {
                    Layout.preferredWidth: root.wideLayout ? 218 : 0
                    Layout.fillHeight: true
                    visible: root.wideLayout
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

                        SidebarButton {
                            Layout.fillWidth: true
                            navIndex: 3
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
                                title: qsTr("General")
                                subtitle: qsTr("Control startup behavior, tray presence, taskbar visibility, and preview menu size.")
                            }

                            Rectangle {
                                width: parent.width
                                height: 72
                                radius: 18
                                color: "#f8fbfc"
                                border.width: 1
                                border.color: "#e1ebf0"

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 14

                                    Column {
                                        Layout.fillWidth: true
                                        spacing: 4

                                        Text {
                                            width: parent.width
                                            text: qsTr("Language")
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 15
                                            font.weight: root.labelFontWeight
                                            renderType: Text.NativeRendering
                                        }

                                        Text {
                                            width: parent.width
                                            text: qsTr("Choose the display language for the Settings page.")
                                            color: root.mutedInkColor
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 12
                                            font.weight: root.bodyFontWeight
                                            elide: Text.ElideRight
                                            renderType: Text.NativeRendering
                                        }
                                    }

                                    ComboBox {
                                        id: languageComboBox
                                        property bool settingsNoDrag: true
                                        Layout.preferredWidth: 132
                                        model: [
                                            { text: qsTr("English"), value: "en" },
                                            { text: qsTr("Chinese"), value: "zh" }
                                        ]
                                        textRole: "text"
                                        valueRole: "value"
                                        currentIndex: resolvedUiSettings && resolvedUiSettings.language === "zh" ? 1 : 0
                                        onActivated: {
                                            resolvedUiSettings.language = currentValue
                                        }
                                    }
                                }
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
                                            text: qsTr("Menu button size")
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 15
                                            font.weight: root.labelFontWeight
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
                                            text: qsTr("Small")
                                            color: root.faintInkColor
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 11
                                            font.weight: root.bodyFontWeight
                                            renderType: Text.NativeRendering
                                        }

                                        Item {
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: qsTr("Large")
                                            color: root.faintInkColor
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 11
                                            font.weight: root.bodyFontWeight
                                            renderType: Text.NativeRendering
                                        }
                                    }
                                }
                            }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: qsTr("Follow system startup")
                                descriptionText: qsTr("Launch SpaceLook automatically when Windows starts.")
                                checkedValue: resolvedUiSettings.autoStart
                                onToggledValue: resolvedUiSettings.autoStart = checked
                            }

                            DividerLine { }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: qsTr("Enable system tray")
                                descriptionText: qsTr("Keep a tray entry available for quick access.")
                                checkedValue: resolvedUiSettings.showSystemTray
                                onToggledValue: resolvedUiSettings.showSystemTray = checked
                            }

                            DividerLine { }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: qsTr("Show preview in taskbar")
                                descriptionText: qsTr("Only affects the preview window. The Settings window stays independent.")
                                checkedValue: resolvedUiSettings.showTaskbar
                                onToggledValue: resolvedUiSettings.showTaskbar = checked
                            }

                            DividerLine { }

                            SettingSwitchRow {
                                width: parent.width
                                labelText: qsTr("Performance mode")
                                descriptionText: qsTr("Use the QML preview path when available for smoother rendering.")
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
                                title: qsTr("Toolbar")
                                subtitle: qsTr("Tune the capsule menu, placement, border, and visible actions.")
                            }

                            Text {
                                text: qsTr("Appearance")
                                color: root.inkColor
                                font.family: root.textFontFamily
                                font.pixelSize: 15
                                font.weight: root.labelFontWeight
                                renderType: Text.NativeRendering
                            }

                            GridLayout {
                                width: parent.width
                                columns: width >= 620 ? 3 : 2
                                columnSpacing: 12
                                rowSpacing: 12

                                ToolbarTile {
                                    titleText: qsTr("Border")
                                    glyph: "\uE8A7"
                                    checkedValue: resolvedUiSettings.showMenuBorder
                                    onToggledValue: resolvedUiSettings.showMenuBorder = checked
                                }
                            }

                            DividerLine { }

                            Column {
                                width: parent.width
                                spacing: 12

                                Text {
                                    text: qsTr("Menu placement")
                                    color: root.inkColor
                                    font.family: root.textFontFamily
                                    font.pixelSize: 15
                                    font.weight: root.labelFontWeight
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
                                        labelText: qsTr("Top")
                                    }

                                    PlacementOptionButton {
                                        Layout.fillWidth: true
                                        placementValue: 1
                                        glyph: "\uE70D"
                                        labelText: qsTr("Bottom")
                                    }

                                    PlacementOptionButton {
                                        Layout.fillWidth: true
                                        placementValue: 2
                                        glyph: "\uE76B"
                                        labelText: qsTr("Left")
                                    }

                                    PlacementOptionButton {
                                        Layout.fillWidth: true
                                        placementValue: 3
                                        glyph: "\uE76C"
                                        labelText: qsTr("Right")
                                    }
                                }
                            }

                            DividerLine { }

                            Text {
                                text: qsTr("Visible actions")
                                color: root.inkColor
                                font.family: root.textFontFamily
                                font.pixelSize: 15
                                font.weight: root.labelFontWeight
                                renderType: Text.NativeRendering
                            }

                            GridLayout {
                                width: parent.width
                                columns: width >= 620 ? 3 : 2
                                columnSpacing: 12
                                rowSpacing: 12

                                ToolbarTile {
                                    titleText: qsTr("Pin")
                                    glyph: "\uE840"
                                    checkedValue: resolvedUiSettings.showMenuPin
                                    onToggledValue: resolvedUiSettings.showMenuPin = checked
                                }

                                ToolbarTile {
                                    titleText: qsTr("Open")
                                    glyph: "\uE8E5"
                                    checkedValue: resolvedUiSettings.showMenuOpen
                                    onToggledValue: resolvedUiSettings.showMenuOpen = checked
                                }

                                ToolbarTile {
                                    titleText: qsTr("Copy")
                                    glyph: "\uE8C8"
                                    checkedValue: resolvedUiSettings.showMenuCopy
                                    onToggledValue: resolvedUiSettings.showMenuCopy = checked
                                }

                                ToolbarTile {
                                    titleText: qsTr("OCR")
                                    glyph: "\uE722"
                                    checkedValue: resolvedUiSettings.showMenuOcr
                                    onToggledValue: resolvedUiSettings.showMenuOcr = checked
                                }

                                ToolbarTile {
                                    titleText: qsTr("Refresh")
                                    glyph: "\uE72C"
                                    checkedValue: resolvedUiSettings.showMenuRefresh
                                    onToggledValue: resolvedUiSettings.showMenuRefresh = checked
                                }

                                ToolbarTile {
                                    titleText: qsTr("Expand")
                                    glyph: "\uE740"
                                    checkedValue: resolvedUiSettings.showMenuExpand
                                    onToggledValue: resolvedUiSettings.showMenuExpand = checked
                                }

                                ToolbarTile {
                                    titleText: qsTr("Close")
                                    glyph: "\uE711"
                                    checkedValue: resolvedUiSettings.showMenuClose
                                    onToggledValue: resolvedUiSettings.showMenuClose = checked
                                }

                                ToolbarTile {
                                    titleText: qsTr("More")
                                    glyph: "\uE712"
                                    checkedValue: resolvedUiSettings.showMenuMore
                                    onToggledValue: resolvedUiSettings.showMenuMore = checked
                                }
                            }
                        }
                    }

                    CardShell {
                        id: ocrSection
                        width: parent.width
                        implicitHeight: ocrContent.implicitHeight + 42

                        Column {
                            id: ocrContent
                            anchors.fill: parent
                            anchors.margins: 22
                            spacing: 18

                            SectionHeader {
                                width: parent.width
                                glyph: "\uE8C8"
                                title: qsTr("OCR")
                                subtitle: qsTr("Configure the text recognition model used by image previews.")
                            }

                            Rectangle {
                                width: parent.width
                                implicitHeight: ocrEnginePanel.implicitHeight + 32
                                radius: 18
                                color: "#f8fbfc"
                                border.width: 1
                                border.color: "#e1ebf0"

                                Column {
                                    id: ocrEnginePanel
                                    anchors.fill: parent
                                    anchors.margins: 16
                                    spacing: 14

                                    Text {
                                        width: parent.width
                                        text: qsTr("OCR engine")
                                        color: root.inkColor
                                        font.family: root.textFontFamily
                                        font.pixelSize: 15
                                        font.weight: root.labelFontWeight
                                        renderType: Text.NativeRendering
                                    }

                                    GridLayout {
                                        width: parent.width
                                        columns: root.wideLayout ? 2 : 1
                                        columnSpacing: 10
                                        rowSpacing: 10

                                        OcrEngineOptionButton {
                                            Layout.fillWidth: true
                                            engineValue: "windows"
                                            labelText: qsTr("Windows OCR")
                                            glyph: "\uE8C8"
                                        }

                                        OcrEngineOptionButton {
                                            Layout.fillWidth: true
                                            engineValue: "baidu"
                                            labelText: qsTr("Baidu OCR")
                                            glyph: "\uE8D4"
                                        }
                                    }

                                    Rectangle {
                                        width: parent.width
                                        height: 38
                                        radius: 12
                                        visible: resolvedUiSettings.ocrEngine === "baidu" &&
                                                 (resolvedUiSettings.baiduOcrApiKey.length === 0 ||
                                                  resolvedUiSettings.baiduOcrSecretKey.length === 0)
                                        color: "#fff6e8"
                                        border.width: 1
                                        border.color: "#f1cf8f"

                                        Text {
                                            anchors.fill: parent
                                            anchors.leftMargin: 12
                                            anchors.rightMargin: 12
                                            text: qsTr("Baidu OCR requires API_KEY and SECRET_KEY.")
                                            color: "#7c5608"
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 12
                                            font.weight: root.labelFontWeight
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                            renderType: Text.NativeRendering
                                        }
                                    }

                                    Column {
                                        width: parent.width
                                        visible: resolvedUiSettings.ocrEngine === "baidu"
                                        spacing: 10

                                        Text {
                                            width: parent.width
                                            text: qsTr("API_KEY")
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 12
                                            font.weight: root.labelFontWeight
                                            renderType: Text.NativeRendering
                                        }

                                        TextField {
                                            width: parent.width
                                            text: resolvedUiSettings.baiduOcrApiKey
                                            placeholderText: qsTr("Enter API_KEY")
                                            selectByMouse: true
                                            color: root.inkColor
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 13
                                            background: Rectangle {
                                                radius: 12
                                                color: "#ffffff"
                                                border.width: 1
                                                border.color: parent.activeFocus ? root.accentColor : root.borderColor
                                            }
                                            onTextChanged: {
                                                if (activeFocus && resolvedUiSettings.baiduOcrApiKey !== text) {
                                                    resolvedUiSettings.baiduOcrApiKey = text
                                                }
                                            }
                                        }

                                        Text {
                                            width: parent.width
                                            text: qsTr("SECRET_KEY")
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 12
                                            font.weight: root.labelFontWeight
                                            renderType: Text.NativeRendering
                                        }

                                        TextField {
                                            width: parent.width
                                            text: resolvedUiSettings.baiduOcrSecretKey
                                            placeholderText: qsTr("Enter SECRET_KEY")
                                            echoMode: TextInput.Password
                                            selectByMouse: true
                                            color: root.inkColor
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 13
                                            background: Rectangle {
                                                radius: 12
                                                color: "#ffffff"
                                                border.width: 1
                                                border.color: parent.activeFocus ? root.accentColor : root.borderColor
                                            }
                                            onTextChanged: {
                                                if (activeFocus && resolvedUiSettings.baiduOcrSecretKey !== text) {
                                                    resolvedUiSettings.baiduOcrSecretKey = text
                                                }
                                            }
                                        }

                                        RowLayout {
                                            width: parent.width
                                            spacing: 10

                                            Button {
                                                id: baiduCredentialTestButton
                                                property bool settingsNoDrag: true
                                                Layout.preferredWidth: 150
                                                Layout.preferredHeight: 38
                                                enabled: !resolvedUiSettings.baiduOcrCredentialTestBusy &&
                                                         resolvedUiSettings.baiduOcrApiKey.length > 0 &&
                                                         resolvedUiSettings.baiduOcrSecretKey.length > 0
                                                hoverEnabled: true
                                                padding: 0
                                                background: Rectangle {
                                                    radius: 12
                                                    color: baiduCredentialTestButton.enabled
                                                           ? (baiduCredentialTestButton.down ? root.accentHoverColor : (baiduCredentialTestButton.hovered ? root.accentHoverColor : root.accentColor))
                                                           : "#d4dee5"
                                                    border.width: 1
                                                    border.color: baiduCredentialTestButton.enabled ? root.accentColor : root.borderColor
                                                }
                                                contentItem: RowLayout {
                                                    anchors.fill: parent
                                                    anchors.leftMargin: 12
                                                    anchors.rightMargin: 12
                                                    spacing: 7

                                                    Text {
                                                        text: "\uE9D9"
                                                        color: "#ffffff"
                                                        font.family: root.iconFontFamily
                                                        font.pixelSize: 13
                                                        renderType: Text.NativeRendering
                                                    }

                                                    Text {
                                                        Layout.fillWidth: true
                                                        text: resolvedUiSettings.baiduOcrCredentialTestBusy ? qsTr("Testing...") : qsTr("Test API Key")
                                                        color: "#ffffff"
                                                        font.family: root.textFontFamily
                                                        font.pixelSize: 12
                                                        font.weight: root.labelFontWeight
                                                        horizontalAlignment: Text.AlignHCenter
                                                        verticalAlignment: Text.AlignVCenter
                                                        elide: Text.ElideRight
                                                        renderType: Text.NativeRendering
                                                    }
                                                }
                                                onClicked: resolvedUiSettings.testBaiduOcrCredentials()
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: resolvedUiSettings.baiduOcrCredentialTestMessage
                                                visible: text.length > 0
                                                color: text === qsTr("Baidu OCR credentials are valid.") ? root.accentColor : root.mutedInkColor
                                                font.family: root.bodyFontFamily
                                                font.pixelSize: 12
                                                font.weight: root.bodyFontWeight
                                                verticalAlignment: Text.AlignVCenter
                                                elide: Text.ElideRight
                                                renderType: Text.NativeRendering
                                            }
                                        }
                                    }
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
                                title: qsTr("File Types")
                                subtitle: qsTr("Edit RenderType.json mappings. Each row maps suffixes to one renderer.")
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
                                    font.weight: root.bodyFontWeight
                                    elide: Text.ElideMiddle
                                    renderType: Text.NativeRendering
                                }
                            }

                            GridLayout {
                                width: parent.width
                                height: root.roomyFileTypeLayout ? 430 : 760
                                columns: root.roomyFileTypeLayout ? 2 : 1
                                columnSpacing: 14
                                rowSpacing: 14

                                Rectangle {
                                    Layout.preferredWidth: root.roomyFileTypeLayout ? 330 : 0
                                    Layout.fillWidth: !root.roomyFileTypeLayout
                                    Layout.preferredHeight: root.roomyFileTypeLayout ? 0 : 300
                                    Layout.fillHeight: root.roomyFileTypeLayout
                                    radius: 18
                                    color: "#f8fbfc"
                                    border.width: 1
                                    border.color: "#e1ebf0"

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        spacing: 10

                                        TextField {
                                            id: renderTypeFilterField
                                            property bool settingsNoDrag: true
                                            width: parent.width
                                            height: 34
                                            placeholderText: qsTr("Search key, renderer, suffix")
                                            text: root.renderTypeFilter
                                            font.family: root.bodyFontFamily
                                            font.pixelSize: 12
                                            onTextChanged: {
                                                root.renderTypeFilter = text
                                                root.rebuildFilteredRenderTypeEntries()
                                            }
                                        }

                                        ListView {
                                            id: renderTypeList
                                            property bool settingsNoDrag: true
                                            width: parent.width
                                            height: parent.height - renderTypeFilterField.height - parent.spacing
                                            clip: true
                                            spacing: 8
                                            model: root.filteredRenderTypeEntries

                                            delegate: Rectangle {
                                                property bool settingsNoDrag: true
                                                property var suffixItems: root.suffixArray(modelData.suffixes || "")
                                                width: renderTypeList.width
                                                height: Math.max(86, listDelegateContent.implicitHeight + 20)
                                                radius: 16
                                                color: modelData.sourceIndex === root.selectedRenderTypeIndex ? root.accentSoftColor : (rowMouse.containsMouse ? "#ffffff" : "transparent")
                                                border.width: modelData.sourceIndex === root.selectedRenderTypeIndex ? 1 : 0
                                                border.color: "#b6dde2"

                                                Column {
                                                    id: listDelegateContent
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 12
                                                    anchors.rightMargin: 12
                                                    anchors.topMargin: 10
                                                    spacing: 6

                                                    RowLayout {
                                                        width: parent.width
                                                        spacing: 8

                                                        Text {
                                                            Layout.fillWidth: true
                                                            text: modelData.key || "Untitled"
                                                            color: root.inkColor
                                                            font.family: root.textFontFamily
                                                            font.pixelSize: 13
                                                            font.weight: root.labelFontWeight
                                                            elide: Text.ElideRight
                                                            renderType: Text.NativeRendering
                                                        }

                                                        Rectangle {
                                                            radius: 10
                                                            color: "#eef6f7"
                                                            implicitWidth: typeKeyText.implicitWidth + 14
                                                            implicitHeight: 22

                                                            Text {
                                                                id: typeKeyText
                                                                anchors.centerIn: parent
                                                                text: modelData.typeKey || "type"
                                                                color: root.accentColor
                                                                font.family: root.bodyFontFamily
                                                                font.pixelSize: 10
                                                                font.weight: root.labelFontWeight
                                                                renderType: Text.NativeRendering
                                                            }
                                                        }
                                                    }

                                                    Text {
                                                        width: parent.width
                                                        text: modelData.name || ""
                                                        color: root.mutedInkColor
                                                        font.family: root.bodyFontFamily
                                                        font.pixelSize: 11
                                                        font.weight: root.bodyFontWeight
                                                        elide: Text.ElideRight
                                                        renderType: Text.NativeRendering
                                                    }

                                                    Flow {
                                                        width: parent.width
                                                        spacing: 5

                                                        Repeater {
                                                            model: Math.min(8, suffixItems.length)

                                                            Rectangle {
                                                                radius: 9
                                                                color: "#ffffff"
                                                                border.width: 1
                                                                border.color: "#dce8ee"
                                                                implicitWidth: suffixText.implicitWidth + 14
                                                                implicitHeight: 22

                                                                Text {
                                                                    id: suffixText
                                                                    anchors.centerIn: parent
                                                                    text: "." + suffixItems[index]
                                                                    color: root.mutedInkColor
                                                                    font.family: root.bodyFontFamily
                                                                    font.pixelSize: 10
                                                                    renderType: Text.NativeRendering
                                                                }
                                                            }
                                                        }

                                                        Rectangle {
                                                            visible: suffixItems.length > 8
                                                            radius: 9
                                                            color: "#eef2f5"
                                                            implicitWidth: moreSuffixText.implicitWidth + 14
                                                            implicitHeight: 22

                                                            Text {
                                                                id: moreSuffixText
                                                                anchors.centerIn: parent
                                                                text: "+" + (suffixItems.length - 8)
                                                                color: root.faintInkColor
                                                                font.family: root.bodyFontFamily
                                                                font.pixelSize: 10
                                                                renderType: Text.NativeRendering
                                                            }
                                                        }
                                                    }
                                                }

                                                MouseArea {
                                                    id: rowMouse
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    onClicked: root.selectRenderTypeEntry(modelData.sourceIndex)
                                                }
                                            }
                                        }
                                    }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: root.roomyFileTypeLayout ? 0 : 446
                                    Layout.fillHeight: root.roomyFileTypeLayout
                                    radius: 18
                                    color: "#fbfdfe"
                                    border.width: 1
                                    border.color: "#e1ebf0"

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 18
                                        spacing: 12

                                        Text {
                                            text: qsTr("Mapping details")
                                            color: root.inkColor
                                            font.family: root.textFontFamily
                                            font.pixelSize: 15
                                            font.weight: root.labelFontWeight
                                            renderType: Text.NativeRendering
                                        }

                                        GridLayout {
                                            width: parent.width
                                            columns: 2
                                            columnSpacing: 12
                                            rowSpacing: 10

                                            Column {
                                                Layout.fillWidth: true
                                                spacing: 2

                                                Text {
                                                    text: qsTr("Group key")
                                                    color: root.faintInkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 11
                                                    font.weight: root.labelFontWeight
                                                    renderType: Text.NativeRendering
                                                }

                                                Text {
                                                    id: renderTypeKeyField
                                                    text: ""
                                                    color: root.inkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 13
                                                    font.weight: root.bodyFontWeight
                                                    renderType: Text.NativeRendering
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Column {
                                                Layout.fillWidth: true
                                                spacing: 2

                                                Text {
                                                    text: qsTr("Renderer")
                                                    color: root.faintInkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 11
                                                    font.weight: root.labelFontWeight
                                                    renderType: Text.NativeRendering
                                                }

                                                Text {
                                                    id: renderTypeNameField
                                                    text: ""
                                                    color: root.inkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 13
                                                    font.weight: root.bodyFontWeight
                                                    renderType: Text.NativeRendering
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Column {
                                                Layout.fillWidth: true
                                                spacing: 2

                                                Text {
                                                    text: qsTr("typeKey")
                                                    color: root.faintInkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 11
                                                    font.weight: root.labelFontWeight
                                                    renderType: Text.NativeRendering
                                                }

                                                Text {
                                                    id: renderTypeTypeKeyField
                                                    text: ""
                                                    color: root.inkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 13
                                                    font.weight: root.bodyFontWeight
                                                    renderType: Text.NativeRendering
                                                    elide: Text.ElideRight
                                                }
                                            }

                                            Column {
                                                Layout.fillWidth: true
                                                spacing: 2

                                                Text {
                                                    text: qsTr("Description")
                                                    color: root.faintInkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 11
                                                    font.weight: root.labelFontWeight
                                                    renderType: Text.NativeRendering
                                                }

                                                Text {
                                                    id: renderTypeDetailsField
                                                    text: ""
                                                    color: root.inkColor
                                                    font.family: root.bodyFontFamily
                                                    font.pixelSize: 13
                                                    font.weight: root.bodyFontWeight
                                                    renderType: Text.NativeRendering
                                                    elide: Text.ElideRight
                                                }
                                            }
                                        }

                                        RowLayout {
                                            width: parent.width
                                            spacing: 10

                                            Text {
                                                text: qsTr("Suffixes")
                                                color: root.inkColor
                                                font.family: root.textFontFamily
                                                font.pixelSize: 13
                                                font.weight: root.labelFontWeight
                                                renderType: Text.NativeRendering
                                            }

                                            Text {
                                                Layout.fillWidth: true
                                                text: root.suffixCountText(renderTypeSuffixesField.text)
                                                color: root.faintInkColor
                                                font.family: root.bodyFontFamily
                                                font.pixelSize: 11
                                                horizontalAlignment: Text.AlignRight
                                                renderType: Text.NativeRendering
                                            }
                                        }

                                        Rectangle {
                                            width: parent.width
                                            height: 160
                                            radius: 16
                                            color: "#ffffff"
                                            border.width: 1
                                            border.color: root.borderColor

                                            Column {
                                                anchors.fill: parent
                                                anchors.margins: 12
                                                spacing: 10

                                                RowLayout {
                                                    width: parent.width
                                                    spacing: 8

                                                    TextField {
                                                        id: renderTypeSuffixInput
                                                        property bool settingsNoDrag: true
                                                        Layout.fillWidth: true
                                                        height: 32
                                                        placeholderText: qsTr("Add suffix, for example tsx")
                                                        font.family: root.bodyFontFamily
                                                        font.pixelSize: 12
                                                        onAccepted: {
                                                            root.addRenderTypeSuffix(text)
                                                            text = ""
                                                        }
                                                    }

                                                    Button {
                                                        property bool settingsNoDrag: true
                                                        text: qsTr("Add suffix")
                                                        enabled: renderTypeSuffixInput.text.trim().length > 0
                                                        onClicked: {
                                                            root.addRenderTypeSuffix(renderTypeSuffixInput.text)
                                                            renderTypeSuffixInput.text = ""
                                                        }
                                                    }
                                                }

                                                Flickable {
                                                    width: parent.width
                                                    height: parent.height - 42
                                                    contentWidth: width
                                                    contentHeight: suffixChipFlow.implicitHeight
                                                    clip: true

                                                    Flow {
                                                        id: suffixChipFlow
                                                        width: parent.width
                                                        spacing: 7

                                                        Repeater {
                                                            model: root.suffixArray(renderTypeSuffixesField.text)

                                                            Rectangle {
                                                                radius: 12
                                                                color: "#f7fbfc"
                                                                border.width: 1
                                                                border.color: "#dce8ee"
                                                                implicitWidth: suffixChipRow.implicitWidth + 16
                                                                implicitHeight: 28

                                                                Row {
                                                                    id: suffixChipRow
                                                                    anchors.centerIn: parent
                                                                    spacing: 6

                                                                    Text {
                                                                        text: "." + modelData
                                                                        color: root.inkColor
                                                                        font.family: root.bodyFontFamily
                                                                        font.pixelSize: 12
                                                                        renderType: Text.NativeRendering
                                                                    }

                                                                    Text {
                                                                        property bool settingsNoDrag: true
                                                                        text: "\uE711"
                                                                        color: suffixRemoveMouse.containsMouse ? root.dangerColor : root.faintInkColor
                                                                        font.family: root.iconFontFamily
                                                                        font.pixelSize: 10
                                                                        renderType: Text.NativeRendering

                                                                        MouseArea {
                                                                            id: suffixRemoveMouse
                                                                            anchors.fill: parent
                                                                            anchors.margins: -6
                                                                            hoverEnabled: true
                                                                            onClicked: root.removeRenderTypeSuffixAt(index)
                                                                        }
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }

                                        TextArea {
                                            id: renderTypeSuffixesField
                                            property bool settingsNoDrag: true
                                            visible: false
                                            height: 0
                                            width: 1
                                            onTextChanged: root.applyRenderTypeEditor()
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
                                                font.weight: root.bodyFontWeight
                                                wrapMode: Text.Wrap
                                                renderType: Text.NativeRendering
                                            }
                                        }

                                        RowLayout {
                                            width: parent.width
                                            spacing: 10

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: qsTr("Add")
                                                onClicked: root.addRenderTypeEntry()
                                            }

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: qsTr("Remove")
                                                enabled: root.selectedRenderTypeIndex >= 0
                                                onClicked: root.removeRenderTypeEntry()
                                            }

                                            Item {
                                                Layout.fillWidth: true
                                            }

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: qsTr("Reload")
                                                onClicked: root.reloadRenderTypeEntries()
                                            }

                                            Button {
                                                property bool settingsNoDrag: true
                                                text: qsTr("Save")
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
}
