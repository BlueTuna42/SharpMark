import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: mainWindow
    width: 1000
    height: 700
    visible: true
    title: "SharpMark"
    
    property bool isLight: backend.themeMode === 1
    
    color: isLight ? "#f5f5f5" : "#1e1e1e"
    property color textColor: isLight ? "#111111" : "#ffffff"
    property color popupBg: isLight ? "#ffffff" : "#2d2d2d"
    property color popupBorder: isLight ? "#cccccc" : "#555555"
    
    property color cardWaitingBg: isLight ? "#ffffff" : "#2a2a2a"
    property color cardWaitingBorder: isLight ? "#cccccc" : "#444444"
    property color cardWaitingText: isLight ? "#666666" : "#aaaaaa"
    
    property color cardBlurryBg: isLight ? "#ffeaea" : "#4a1c1c"
    property color cardBlurryBorder: isLight ? "#ff8888" : "#ff4444"
    property color cardBlurryText: isLight ? "#cc0000" : "#ff8888"
    
    property color cardNeutralBg: isLight ? "#ffffff" : "#2a2a2a"
    property color cardNeutralBorder: isLight ? "#cccccc" : "#444444"
    property color cardNeutralText: isLight ? "#666666" : "#aaaaaa"

property bool isGridView: true
    property bool pipelineVisible: false // Property for the left sidebar
    property bool groupBursts: backend.groupBursts
    property bool showPhotoControls: true // Collapsible photo controls (rows 2 & 3)

    // --- Multi-select state ---
    property var selectedPaths: ({})   // object used as a set: { filePath: true }
    property int selectedCount: 0
    property int lastClickedProxyIndex: -1   // for Shift-range selection

    function selectSingle(filePath, proxyIndex) {
        selectedPaths = {}
        var obj = {}
        obj[filePath] = true
        selectedPaths = obj
        selectedCount = 1
        lastClickedProxyIndex = proxyIndex
    }

    function toggleOne(filePath, proxyIndex) {
        var obj = Object.assign({}, selectedPaths)
        if (obj[filePath]) {
            delete obj[filePath]
            selectedCount--
        } else {
            obj[filePath] = true
            selectedCount++
        }
        selectedPaths = obj
        lastClickedProxyIndex = proxyIndex
    }

    function selectRange(toProxyIndex) {
        if (lastClickedProxyIndex < 0) return
        var from = Math.min(lastClickedProxyIndex, toProxyIndex)
        var to   = Math.max(lastClickedProxyIndex, toProxyIndex)
        var obj = Object.assign({}, selectedPaths)
        for (var i = from; i <= to; i++) {
            var sourceRow = backend.burstProxy.mapToSourceRow(i)
            if (sourceRow >= 0 && sourceRow < resultsModel.count) {
                obj[resultsModel.get(sourceRow).filePath] = true
            }
        }
        selectedPaths = obj
        selectedCount = Object.keys(selectedPaths).length
        // lastClickedProxyIndex stays as the anchor
    }

    function clearSelection() {
        selectedPaths = {}
        selectedCount = 0
        lastClickedProxyIndex = -1
    }
    
    // Calculate score color: 
    // - Below 4: red-orange (hue 0-15)
    // - 4-7: smooth gradient orange to green
    // - Above 7: vibrant deep green (hue 120-130)
    function getScoreColor(score) {
        score = Math.max(0, Math.min(10, score))
        
        var hue
        if (score <= 4) {
            // 0-4: red (0) to orange (15) - very red/orange
            hue = (score / 4) * 15
        } else if (score <= 7) {
            // 4-7: orange (15) to green (120)
            hue = 15 + ((score - 4) / 3) * 105
        } else {
            // 7-10: green (120) to vibrant deep green (130)
            hue = 120 + ((score - 7) / 3) * 10
        }
        
        var saturation = 0.9
        var value = 0.85
        
        // Convert HSV to RGB
        var c = value * saturation
        var x = c * (1 - Math.abs((hue / 60) % 2 - 1))
        var m = value - c
        
        var r, g, b
        if (hue < 60) {
            r = c; g = x; b = 0
        } else if (hue < 120) {
            r = x; g = c; b = 0
        } else if (hue < 180) {
            r = 0; g = c; b = x
        } else if (hue < 240) {
            r = 0; g = x; b = c
        } else if (hue < 300) {
            r = x; g = 0; b = c
        } else {
            r = c; g = 0; b = x
        }
        
        r = Math.round((r + m) * 255)
        g = Math.round((g + m) * 255)
        b = Math.round((b + m) * 255)
        
        return "#" + r.toString(16).padStart(2, '0') + g.toString(16).padStart(2, '0') + b.toString(16).padStart(2, '0')
    }

    function toggleGroupExpansion(proxyIndex) {
        let sourceIndex = backend.burstProxy.mapToSourceRow(proxyIndex)
        let expandedState = !resultsModel.get(sourceIndex).isExpanded
        resultsModel.setProperty(sourceIndex, "isExpanded", expandedState)
        
        for (let i = sourceIndex + 1; i < resultsModel.count; i++) {
            if (resultsModel.get(i).isLead) break;
            resultsModel.setProperty(i, "isExpanded", expandedState)
        }
    }

    Component.onCompleted: {
        backend.burstProxy.source = resultsModel
        backend.burstProxy.groupBursts = Qt.binding(() => mainWindow.groupBursts)
    }

    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        enabled: mainWindow.selectedCount > 0
        onActivated: mainWindow.clearSelection()
    }

    Shortcut {
        sequence: "Delete"
        context: Qt.ApplicationShortcut
        enabled: mainWindow.selectedCount > 0 && !viewerWindow.visible
        onActivated: trashMultiConfirmDialog.openForSelection()
    }

    // ── Open in external editor (E) ─────────────────────────────────────────
    Shortcut {
        sequence: "e"
        context: Qt.ApplicationShortcut
        enabled: (viewerWindow.visible || mainWindow.selectedCount > 0) && backend.externalEditorPath !== ""
        onActivated: {
            if (viewerWindow.visible && viewerWindow.currentFilePath !== "") {
                backend.openInExternalEditor([viewerWindow.currentFilePath])
            } else {
                backend.openInExternalEditor(Object.keys(mainWindow.selectedPaths))
            }
        }
    }

    // ── Rating & colour-label shortcuts ─────────────────────────────────────
    // Apply to the image currently open in the viewer, OR to every selected
    // image in the grid when the viewer is closed.

    function applyRating(rating) {
        if (viewerWindow.visible && viewerWindow.currentFilePath !== "") {
            // Viewer is open → act on the single displayed image
            const next = (viewerWindow.currentRating === rating) ? 0 : rating
            viewerWindow.currentRating = next
            backend.setPhotoRating(viewerWindow.currentFilePath, next)
        } else {
            // Grid mode → act on all selected images
            const paths = Object.keys(mainWindow.selectedPaths)
            for (let i = 0; i < paths.length; i++)
                backend.setPhotoRating(paths[i], rating)
        }
    }

    function applyColorLabel(label) {
        if (viewerWindow.visible && viewerWindow.currentFilePath !== "") {
            const next = (viewerWindow.currentColorLabel === label) ? "" : label
            viewerWindow.currentColorLabel = next
            backend.setPhotoColorLabel(viewerWindow.currentFilePath, next)
        } else {
            const paths = Object.keys(mainWindow.selectedPaths)
            for (let i = 0; i < paths.length; i++)
                backend.setPhotoColorLabel(paths[i], label)
        }
    }

    // 0 — clear star rating only
    function applyClearRating() {
        if (viewerWindow.visible && viewerWindow.currentFilePath !== "") {
            viewerWindow.currentRating = 0
            backend.setPhotoRating(viewerWindow.currentFilePath, 0)
        } else {
            const paths = Object.keys(mainWindow.selectedPaths)
            for (let i = 0; i < paths.length; i++)
                backend.setPhotoRating(paths[i], 0)
        }
    }

    // o — remove colour label only
    function applyClearLabel() {
        if (viewerWindow.visible && viewerWindow.currentFilePath !== "") {
            viewerWindow.currentColorLabel = ""
            backend.setPhotoColorLabel(viewerWindow.currentFilePath, "")
        } else {
            const paths = Object.keys(mainWindow.selectedPaths)
            for (let i = 0; i < paths.length; i++)
                backend.setPhotoColorLabel(paths[i], "")
        }
    }

    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyRating(1) }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyRating(2) }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyRating(3) }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyRating(4) }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyRating(5) }
    Shortcut { sequence: "0"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyClearRating() }
    Shortcut { sequence: "o"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyClearLabel() }
    Shortcut { sequence: "6"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyColorLabel("Red") }
    Shortcut { sequence: "7"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyColorLabel("Yellow") }
    Shortcut { sequence: "8"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyColorLabel("Green") }
    Shortcut { sequence: "9"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyColorLabel("Blue") }
    Shortcut { sequence: "p"; context: Qt.ApplicationShortcut; enabled: viewerWindow.visible || mainWindow.selectedCount > 0; onActivated: mainWindow.applyColorLabel("Purple") }
    // ────────────────────────────────────────────────────────────────────────

    component StyledButton : Button {
        id: control
        property bool isPrimary: false
        
        hoverEnabled: true

        contentItem: Text {
            text: control.text
            font.pixelSize: 13
            font.weight: control.isPrimary ? Font.Bold : Font.Normal
            color: control.enabled ? (control.isPrimary ? "#ffffff" : textColor) : (isLight ? "#999999" : "#666666")
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            implicitWidth: Math.max(110, control.contentItem.implicitWidth + 30)
            implicitHeight: 34
            radius: 6
            color: {
                if (control.isPrimary) {
                    return control.down ? "#004a99" : (control.hovered ? "#0066cc" : "#005bb5")
                } else {
                    return control.down ? (isLight ? "#d0d0d0" : "#1a1a1a") :
                           (control.hovered ? (isLight ? "#e0e0e0" : "#333333") :
                                              (isLight ? "#f5f5f5" : "#252525"))
                }
            }
            border.color: control.isPrimary ? "#004a99" : popupBorder
            border.width: 1
            opacity: control.enabled ? 1.0 : 0.4
            
            Behavior on color { ColorAnimation { duration: 100 } }
        }
        
        // Hand cursor on hover
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            acceptedButtons: Qt.NoButton // Let the parent Button handle the actual click
        }
    }

    component StyledComboBox : ComboBox {
        id: control
        implicitHeight: 34
        
        delegate: ItemDelegate {
            width: control.popup.width
            height: 34
            contentItem: Text {
                text: modelData
                color: control.highlightedIndex === index ? "#ffffff" : textColor
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                leftPadding: 10
            }
            background: Rectangle {
                radius: 4
                anchors.fill: parent
                anchors.margins: 2
                color: control.highlightedIndex === index ? "#0066cc" : "transparent"
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    control.currentIndex = index
                    control.activated(index)
                    control.popup.close()
                }
            }
        }

        contentItem: Text {
            leftPadding: 15
            rightPadding: 30
            text: control.currentText
            font.pixelSize: 13
            font.weight: Font.Medium
            color: textColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        background: Rectangle {
            implicitWidth: Math.max(140, control.contentItem.implicitWidth + 40)
            implicitHeight: 34
            radius: 6
            color: control.down ? (isLight ? "#d0d0d0" : "#1a1a1a") : (control.hovered ? (isLight ? "#e0e0e0" : "#333333") : (isLight ? "#ffffff" : "#252525"))
            border.color: popupBorder
            border.width: 1
            
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text: "▼"
                font.pixelSize: 10
                color: isLight ? "#666" : "#aaa"
            }
            Behavior on color { ColorAnimation { duration: 100 } }
        }

        popup: Popup {
            y: control.height + 4
            width: control.width
            implicitHeight: contentItem.implicitHeight + 8
            padding: 4

            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: control.popup.visible ? control.delegateModel : null
                currentIndex: control.highlightedIndex
            }

            background: Rectangle {
                color: popupBg
                border.color: popupBorder
                border.width: 1
                radius: 6
            }
        }
    }

    ListModel { id: resultsModel }

    Connections {
        target: backend

        function onFileFound(fileName, filePath, index) {
            resultsModel.append({
                fileName: fileName,
                filePath: filePath,
                isRejected: false,
                rejectReason: "",
                score: "0.00",
                status: "WAITING",
                isLead: true,  
                groupCount: 1,
                isExpanded: false,
                colorLabel: "",
                rating: 0,
                isBestShot: false
            })
        }

        function onFileMetadataLoaded(index, rating, colorLabel) {
            if (index >= 0 && index < resultsModel.count) {
                if (rating > 0)        resultsModel.setProperty(index, "rating", rating)
                if (colorLabel !== "") resultsModel.setProperty(index, "colorLabel", colorLabel)
            }
        }

        function onFileProcessed(index, isRejected, rejectReason, aestheticScore, width, height) {
            if (index >= 0 && index < resultsModel.count) {
                resultsModel.setProperty(index, "isRejected", isRejected)
                resultsModel.setProperty(index, "rejectReason", rejectReason)
                resultsModel.setProperty(index, "score", aestheticScore.toFixed(2))
                resultsModel.setProperty(index, "status", isRejected ? "REJECTED" : "ACCEPTED")
            }
        }

        function onGroupAssigned(index, leadIndex, isLead, groupSize) {
            if (index >= 0 && index < resultsModel.count) {
                resultsModel.setProperty(index, "isLead", isLead)
                resultsModel.setProperty(index, "groupCount", groupSize)
            }
        }

        function onBestShotAssigned(index, isBestShot, leadIndex) {
            if (index >= 0 && index < resultsModel.count) {
                resultsModel.setProperty(index, "isBestShot", isBestShot)
            }
        }

        function onColorLabelChanged(filePath, label) {
            for (let i = 0; i < resultsModel.count; i++) {
                if (resultsModel.get(i).filePath === filePath) {
                    resultsModel.setProperty(i, "colorLabel", label)
                    break
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: { resultsModel.clear(); backend.selectFolder(folderDialog.selectedFolder) }
    }

    // ── Pipeline Step Settings Popup ───────────────────────────────────────
    Popup {
        id: stepSettingsPopup
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        property int minWidth: 380
        width: Math.max(minWidth, titleMetrics.advanceWidth + 80)
        height: 280
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        property string stepId: ""
        property string stepName: ""
        property var stepModel: null
        property int stepIndex: -1

        TextMetrics {
            id: titleMetrics
            font.pixelSize: 20
            font.bold: true
            text: stepSettingsPopup.stepName + " Settings"
        }

        background: Rectangle {
            color: popupBg
            radius: 8
            border.color: popupBorder
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Text {
                text: stepSettingsPopup.stepName + " Settings"
                color: textColor
                font.pixelSize: 20
                font.bold: true
                Layout.alignment: Qt.AlignHCenter
            }

            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: popupBorder
            }

            // Exposure settings
            ColumnLayout {
                visible: stepSettingsPopup.stepId === "exposure"
                spacing: 10
                Layout.fillWidth: true

                Text { text: "Clip Threshold"; color: textColor; font.pixelSize: 13 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Slider {
                        id: exposureSlider
                        Layout.fillWidth: true
                        from: 0.0; to: 1.0; stepSize: 0.01
                        value: 0.15
                        onValueChanged: exposureValue.text = value.toFixed(2)
                    }
                    Text {
                        id: exposureValue
                        text: "0.15"
                        color: textColor
                        font.pixelSize: 13
                        Layout.preferredWidth: 40
                    }
                }
                Text {
                    text: "Reject image if more than this fraction of pixels are clipped."
                    color: isLight ? "#888" : "#aaa"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Laplacian settings
            ColumnLayout {
                visible: stepSettingsPopup.stepId === "laplacian"
                spacing: 10
                Layout.fillWidth: true

                Text { text: "Focus Threshold"; color: textColor; font.pixelSize: 13 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Slider {
                        id: laplacianSlider
                        Layout.fillWidth: true
                        from: 10.0; to: 500.0; stepSize: 1.0
                        value: 150.0
                        onValueChanged: laplacianValue.text = value.toFixed(0)
                    }
                    Text {
                        id: laplacianValue
                        text: "150"
                        color: textColor
                        font.pixelSize: 13
                        Layout.preferredWidth: 40
                    }
                }
                Text {
                    text: "Images with Laplacian variance below this value are marked blurry."
                    color: isLight ? "#888" : "#aaa"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Visual Hash grouping settings
            ColumnLayout {
                visible: stepSettingsPopup.stepId === "visual_hash"
                spacing: 10
                Layout.fillWidth: true

                Text { text: "Hamming Distance Threshold"; color: textColor; font.pixelSize: 13 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Slider {
                        id: hashThresholdSlider
                        Layout.fillWidth: true
                        from: 1; to: 64; stepSize: 1
                        value: 20
                        onValueChanged: hashThresholdValue.text = value.toFixed(0)
                    }
                    Text {
                        id: hashThresholdValue
                        text: "20"
                        color: textColor
                        font.pixelSize: 13
                        Layout.preferredWidth: 40
                    }
                }
                Text {
                    text: "Max bit differences for two images to be grouped. Lower = stricter grouping."
                    color: isLight ? "#888" : "#aaa"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // CLIP Embedding grouping settings
            ColumnLayout {
                visible: stepSettingsPopup.stepId === "clip_embedding"
                spacing: 10
                Layout.fillWidth: true

                Text { text: "Cosine Similarity Threshold"; color: textColor; font.pixelSize: 13 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Slider {
                        id: clipThresholdSlider
                        Layout.fillWidth: true
                        from: 0.5; to: 1.0; stepSize: 0.01
                        value: 0.90
                        onValueChanged: clipThresholdValue.text = value.toFixed(2)
                    }
                    Text {
                        id: clipThresholdValue
                        text: "0.90"
                        color: textColor
                        font.pixelSize: 13
                        Layout.preferredWidth: 40
                    }
                }
                Text {
                    text: "Minimum cosine similarity for two images to be grouped. Higher = stricter grouping."
                    color: isLight ? "#888" : "#aaa"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // AI Aesthetic settings
            ColumnLayout {
                visible: stepSettingsPopup.stepId === "aiaesthetic"
                spacing: 10
                Layout.fillWidth: true

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    CheckBox {
                        id: showScoreCheck
                        checked: true
                    }
                    Text {
                        text: "Show AI Score"
                        color: textColor
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    CheckBox {
                        id: colorScoreCheck
                        checked: true
                    }
                    Text {
                        text: "Color AI Score"
                        color: textColor
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    CheckBox {
                        id: applyBiasCheck
                        checked: true
                    }
                    Text {
                        text: "Apply User Bias"
                        color: textColor
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }
                }

                Text {
                    text: "User bias adjusts the AI score based on your star rating history."
                    color: isLight ? "#888" : "#aaa"
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // Default empty state for steps without settings
            ColumnLayout {
                visible: stepSettingsPopup.stepId !== "exposure"
                         && stepSettingsPopup.stepId !== "laplacian"
                         && stepSettingsPopup.stepId !== "visual_hash"
                         && stepSettingsPopup.stepId !== "clip_embedding"
                         && stepSettingsPopup.stepId !== "aiaesthetic"
                Layout.fillWidth: true
                spacing: 10
                Text {
                    text: "No configurable settings for this step."
                    color: isLight ? "#888" : "#aaa"
                    font.pixelSize: 13
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
            }

            Item { Layout.fillHeight: true }

            RowLayout {
                Layout.fillWidth: true
                spacing: 10
                Item { Layout.fillWidth: true }
                StyledButton {
                    text: "Cancel"
                    onClicked: stepSettingsPopup.close()
                }
                StyledButton {
                    text: "Apply"
                    isPrimary: true
                    onClicked: {
                        var settings = ({})
                        if (stepSettingsPopup.stepId === "exposure") {
                            settings["clipThreshold"] = exposureSlider.value
                        } else if (stepSettingsPopup.stepId === "laplacian") {
                            settings["focusThreshold"] = laplacianSlider.value
                        } else if (stepSettingsPopup.stepId === "visual_hash") {
                            settings["hammingThreshold"] = hashThresholdSlider.value
                        } else if (stepSettingsPopup.stepId === "clip_embedding") {
                            settings["cosineThreshold"] = clipThresholdSlider.value
                        } else if (stepSettingsPopup.stepId === "aiaesthetic") {
                            settings["showScore"] = showScoreCheck.checked
                            settings["colorScore"] = colorScoreCheck.checked
                            settings["applyUserBias"] = applyBiasCheck.checked
                        }
                        if (stepSettingsPopup.stepModel) {
                            stepSettingsPopup.stepModel.setStepSettings(
                                stepSettingsPopup.stepIndex, settings)
                        }
                        stepSettingsPopup.close()
                    }
                }
            }
        }
    }

    Popup {
        id: settingsPopup
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: 450
        height: 350
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: popupBg
            radius: 8
            border.color: popupBorder
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 20
            spacing: 15

            Text { text: "Settings"; color: textColor; font.pixelSize: 22; font.bold: true; Layout.alignment: Qt.AlignHCenter }

            GridLayout {
                columns: 2
                rowSpacing: 15
                columnSpacing: 15

                Text { text: "Theme:"; color: textColor }
                ComboBox {
                    model: ["System", "Light", "Dark"]
                    currentIndex: backend.themeMode
                    onActivated: backend.themeMode = currentIndex
                    Layout.fillWidth: true
                }

                Text { text: "External editor:"; color: textColor }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    TextField {
                        id: editorPathField
                        Layout.fillWidth: true
                        placeholderText: "Path to Photoshop, Lightroom, etc."
                        text: backend.externalEditorPath
                        color: textColor
                        background: Rectangle {
                            color: isLight ? "#f5f5f5" : "#1e1e1e"
                            border.color: popupBorder
                            radius: 4
                        }
                        onEditingFinished: backend.externalEditorPath = text
                    }
                    StyledButton {
                        text: "Browse..."
                        onClicked: editorFileDialog.open()
                    }
                    FileDialog {
                        id: editorFileDialog
                        title: "Select external editor executable"
                        nameFilters: ["Executables (*.exe *.app)", "All files (*)"]
                        onAccepted: {
                            let path = selectedFile.toString()
                            // Strip file:/// prefix
                            path = path.replace(/^file:\/\/\//, "").replace(/^file:\/\//, "")
                            editorPathField.text = path
                            backend.externalEditorPath = path
                        }
                    }
                }

                Text { text: "RAW View Quality:"; color: textColor }
                ComboBox {
                    model: ["Thumbnail (Fastest)", "Half size", "Full size"]
                    currentIndex: backend.rawViewMode
                    onActivated: backend.rawViewMode = currentIndex
                    Layout.fillWidth: true
                }

                Text { text: "RAW Analysis Quality:"; color: textColor }
                ComboBox {
                    model: ["Thumbnail (Fastest)", "Half size", "Full size"]
                    currentIndex: backend.rawAnalysisMode
                    onActivated: backend.rawAnalysisMode = currentIndex
                    Layout.fillWidth: true
                }

                Text { text: "Debug log:"; color: textColor }
                StyledButton {
                    text: "Open Log Location"
                    onClicked: {
                        var logPath = backend.logFilePath()
                        var folderUrl = "file:///" + logPath.replace(/\\/g, "/").replace(/\/[^/]+$/, "/")
                        Qt.openUrlExternally(folderUrl)
                    }
                }
            }
            Item { Layout.fillHeight: true }

            StyledButton { text: "OK"; Layout.alignment: Qt.AlignHCenter; onClicked: settingsPopup.close() }
        }
    }
    
    MessageDialog {
        id: trashConfirmDialog
        title: "Move to Trash"
        text: "Are you sure you want to move this photo to the system Trash?"
        buttons: MessageDialog.Yes | MessageDialog.No
        
        property int targetIndex: -1
        
        onButtonClicked: function(button, role) {
            if (button === MessageDialog.Yes && targetIndex !== -1) {
                let file = resultsModel.get(targetIndex).filePath;
                if (backend.trashFile(file)) {
                    resultsModel.remove(targetIndex);
                    
                    if (resultsModel.count === 0) {
                        viewerWindow.close();
                    } else {
                        if (targetIndex >= resultsModel.count) {
                            viewerWindow.currentIndex = resultsModel.count - 1;
                        } else {
                            let temp = targetIndex;
                            viewerWindow.currentIndex = -1;
                            viewerWindow.currentIndex = temp;
                        }
                    }
                }
            }
            targetIndex = -1;
        }
    }

    MessageDialog {
        id: trashMultiConfirmDialog
        title: "Move to Trash"
        buttons: MessageDialog.Yes | MessageDialog.No

        property var pathsToTrash: []

        function openForSelection() {
            pathsToTrash = Object.keys(mainWindow.selectedPaths)
            var n = pathsToTrash.length
            text = "Are you sure you want to move " + n + " selected photo" + (n !== 1 ? "s" : "") + " to the system Trash?"
            open()
        }

        onButtonClicked: function(button, role) {
            if (button === MessageDialog.Yes) {
                // Collect source indices to remove (must map through proxy)
                var indicesToRemove = []
                for (var i = 0; i < pathsToTrash.length; i++) {
                    var fp = pathsToTrash[i]
                    backend.trashFile(fp)
                    // Find the source index in resultsModel
                    for (var j = 0; j < resultsModel.count; j++) {
                        if (resultsModel.get(j).filePath === fp) {
                            indicesToRemove.push(j)
                            break
                        }
                    }
                }
                // Remove in reverse order so indices stay valid
                indicesToRemove.sort(function(a, b) { return b - a })
                for (var k = 0; k < indicesToRemove.length; k++) {
                    resultsModel.remove(indicesToRemove[k])
                }
                mainWindow.clearSelection()
                if (resultsModel.count === 0) {
                    viewerWindow.close()
                }
            }
            pathsToTrash = []
        }
    }

    Window {
        id: viewerWindow
        width: 1200
        height: 800
        title: "Viewer"
        color: "#050505"
        visible: false

        property int currentIndex: -1
        property string exifInfo: ""
        property bool sidebarVisible: true
        
        property int currentRating: 0
        property string currentFilePath: ""
        property string currentColorLabel: ""

        // Maps label string -> hex color used for UI accents
        function labelColor(lbl) {
            if (lbl === "Red")    return "#e05050"
            if (lbl === "Yellow") return "#d4b800"
            if (lbl === "Green")  return "#3caa3c"
            if (lbl === "Blue")   return "#3a80d2"
            if (lbl === "Purple") return "#9b59b6"
            return ""
        }

        onCurrentIndexChanged: {
            if (currentIndex >= 0 && currentIndex < backend.burstProxy.count) {
                const file = backend.burstProxy.get(currentIndex).filePath
                
                viewerWindow.currentFilePath = file;
                
                viewerImage.scale = 1.0
                flickable.contentX = 0
                flickable.contentY = 0
                
                viewerImage.source = ""
                viewerImage.source = "image://full/" + encodeURIComponent(file)
                title = "Viewer - " + backend.burstProxy.get(currentIndex).fileName
                
                let meta = backend.getPhotoMetadata(encodeURIComponent(file));
                exifInfo = meta.infoText;
                
                viewerWindow.currentRating     = backend.getPhotoRating(file);
                viewerWindow.currentColorLabel = backend.getPhotoColorLabel(file);

                console.log("[QML] Photo changed. New Path:", viewerWindow.currentFilePath, "Rating:", viewerWindow.currentRating);
            }
        }

        // Reload the current image whenever the active LUT changes
        Connections {
            target: backend
            function onActiveLutChanged() {
                if (viewerWindow.visible && viewerWindow.currentIndex >= 0
                        && viewerWindow.currentIndex < backend.burstProxy.count) {
                    const file = backend.burstProxy.get(viewerWindow.currentIndex).filePath
                    viewerImage.source = ""
                    viewerImage.source = "image://full/" + encodeURIComponent(file)
                }
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                Flickable {
                    id: flickable
                    anchors.fill: parent
                    contentWidth: viewerImage.width * viewerImage.scale
                    contentHeight: viewerImage.height * viewerImage.scale
                    boundsBehavior: Flickable.StopAtBounds
                    clip: true

                    Image {
                        id: viewerImage
                        width: flickable.width
                        height: flickable.height
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        cache: false
                        transformOrigin: Item.TopLeft
                        smooth: true
                        mipmap: true 

                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.NoButton 
                            
                            onWheel: (wheel) => {
                                let zoomFactor = wheel.angleDelta.y > 0 ? 1.15 : 1 / 1.15;
                                let oldScale = viewerImage.scale;
                                let newScale = Math.max(1.0, Math.min(oldScale * zoomFactor, 15.0));
                                viewerImage.scale = newScale;
                                let ratio = newScale / oldScale;
                                flickable.contentX = flickable.contentX * ratio + wheel.x * (ratio - 1);
                                flickable.contentY = flickable.contentY * ratio + wheel.y * (ratio - 1);
                            }
                        }
                    }
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    running: viewerImage.status === Image.Loading
                    width: 64
                    height: 64
                }

                RowLayout {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.margins: 30
                    spacing: 20

                    StyledButton {
                        text: "<- Previous"
                        onClicked: if (viewerWindow.currentIndex > 0) viewerWindow.currentIndex--
                        enabled: viewerWindow.currentIndex > 0
                    }
                    
                    // Lightroom-style star rating
                    Rectangle {
                        color: "#aa000000" 
                        radius: 8
                        Layout.preferredHeight: 40
                        Layout.preferredWidth: 180
                        z: 10
                        
                        Row {
                            anchors.centerIn: parent
                            spacing: 8
                            
                            Repeater {
                                model: 5
                                delegate: Text {
                                    property int starVal: index + 1
                                    property string lblColor: viewerWindow.labelColor(viewerWindow.currentColorLabel)

                                    text: starVal <= viewerWindow.currentRating ? "★" : "☆"
                                    // Filled stars: gold. Empty stars: label color if set, else grey.
                                    color: starVal <= viewerWindow.currentRating
                                           ? "#FFD700"
                                           : (lblColor !== "" ? lblColor : "#aaaaaa")
                                    font.pixelSize: 32
                                    
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        
                                        onClicked: {
                                            console.log("[QML] Star clicked:", starVal);
                                            
                                            if (viewerWindow.currentRating === starVal) {
                                                viewerWindow.currentRating = 0; 
                                            } else {
                                                viewerWindow.currentRating = starVal;
                                            }
                                            
                                            console.log("[QML] New rating:", viewerWindow.currentRating, "Path:", viewerWindow.currentFilePath);
                                            backend.setPhotoRating(viewerWindow.currentFilePath, viewerWindow.currentRating);
                                        }
                                    }
                                }
                            }
                        }
                    }

                    StyledButton {
                        text: "Next ->"
                        onClicked: if (viewerWindow.currentIndex < backend.burstProxy.count - 1) viewerWindow.currentIndex++
                        enabled: viewerWindow.currentIndex < backend.burstProxy.count - 1
                    }
                }


            }
            
            Rectangle {
                id: sidebar
                Layout.preferredWidth: viewerWindow.sidebarVisible ? 300 : 0
                Layout.fillHeight: true
                color: "#1a1a1a"
                clip: true
                
                Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }

                Rectangle {
                    width: 1
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    color: "#333333"
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    visible: sidebar.Layout.preferredWidth > 100

                    Text {
                        text: "Metadata & Analysis"
                        color: "white"
                        font.bold: true
                        font.pixelSize: 16
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        color: "#252525"
                        border.color: "#444"
                        radius: 4
                        clip: true

                        Image {
                            id: histogramImage
                            anchors.fill: parent
                            fillMode: Image.PreserveAspectFit
                            source: backend.histogramBase64 !== "" ? backend.histogramBase64 : ""
                            
                            Text {
                                anchors.centerIn: parent
                                text: "Loading histogram..."
                                color: "#888"
                                visible: backend.histogramBase64 === ""
                            }
                        }
                    }

                    Text {
                        text: viewerWindow.exifInfo
                        color: "#dddddd"
                        font.pixelSize: 14
                        lineHeight: 1.4
                        textFormat: Text.RichText
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        wrapMode: Text.WordWrap
                        verticalAlignment: Text.AlignTop
                        onLinkActivated: (link) => Qt.openUrlExternally(link)
                    }
                }
            }
        }

        StyledButton {
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: 15
            text: viewerWindow.sidebarVisible ? "▶" : "◀"
            width: 40
            onClicked: viewerWindow.sidebarVisible = !viewerWindow.sidebarVisible
        }

        Shortcut { sequence: "Left"; onActivated: if (viewerWindow.currentIndex > 0) viewerWindow.currentIndex-- }
        Shortcut { sequence: "Right"; onActivated: if (viewerWindow.currentIndex < backend.burstProxy.count - 1) viewerWindow.currentIndex++ }
        Shortcut { sequence: "Escape"; onActivated: viewerWindow.close() }
        Shortcut {
            sequence: "Delete"
            enabled: viewerWindow.visible
            onActivated: {
                trashConfirmDialog.targetIndex = viewerWindow.currentIndex;
                trashConfirmDialog.open();
            }
        }
    }

    // ==========================================
    // MAIN INTERFACE (LEFT SIDEBAR + RIGHT CONTENT)
    // ==========================================
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // LEFT SIDEBAR (Pipeline)
        Rectangle {
            id: pipelineSidebar
            Layout.preferredWidth: mainWindow.pipelineVisible ? 280 : 0
            Layout.fillHeight: true
            color: popupBg
            clip: true
            
            Behavior on Layout.preferredWidth { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }

            Rectangle {
                width: 1
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                color: popupBorder
            }

            ScrollView {
                id: sidebarScroll
                anchors.fill: parent
                anchors.margins: 15
                visible: pipelineSidebar.Layout.preferredWidth > 50
                clip: true
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

            Column {
                width: sidebarScroll.width
                spacing: 15

                Text { text: "Pipeline Config"; color: textColor; font.bold: true; font.pixelSize: 16 }

                // ---- PREPROCESSORS section ----
                Text {
                    text: "Preprocessors"
                    color: isLight ? "#555" : "#bbb"
                    font.pixelSize: 12
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                }

                ListView {
                    id: preprocessorList
                    width: parent.width
                    implicitHeight: contentHeight
                    clip: true
                    spacing: 4
                    interactive: false
                    model: backend.preprocessorModel

                    delegate: Rectangle {
                        width: preprocessorList.width
                        height: 40
                        radius: 4
                        color: isLight ? "#f0f0f0" : "#2a2a2a"
                        border.color: popupBorder

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            spacing: 8

                            CheckBox {
                                enabled: model.supportsDisable
                                checked: model.enabled
                                opacity: model.supportsDisable ? 1.0 : 0.4
                                onToggled: {
                                    if (model.id === "visual_hash") {
                                        backend.setGroupingMode(checked ? "visual_hash" : "none")
                                    } else {
                                        backend.preprocessorModel.setStepEnabled(model.index, checked)
                                    }
                                }
                            }

                            Text {
                                text: model.name
                                color: model.enabled ? textColor : (isLight ? "#999" : "#666")
                                Layout.fillWidth: true
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }

                            // Gear icon for step settings
                            Button {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                flat: true
                                hoverEnabled: true
                                opacity: parent.hovered ? 0.8 : 0.35
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                                onClicked: {
                                    stepSettingsPopup.stepId = model.id
                                    stepSettingsPopup.stepName = model.name
                                    stepSettingsPopup.stepModel = backend.preprocessorModel
                                    stepSettingsPopup.stepIndex = model.index

                                    var s = stepSettingsPopup.stepModel.getStepSettings(stepSettingsPopup.stepIndex) || ({})
                                    if (model.id === "exposure") exposureSlider.value = s.clipThreshold !== undefined ? s.clipThreshold : 0.15
                                    if (model.id === "laplacian") laplacianSlider.value = s.focusThreshold !== undefined ? s.focusThreshold : 150.0
                                    if (model.id === "visual_hash") hashThresholdSlider.value = s.hammingThreshold !== undefined ? s.hammingThreshold : 20
                                    if (model.id === "clip_embedding") clipThresholdSlider.value = s.cosineThreshold !== undefined ? s.cosineThreshold : 0.90
                                    if (model.id === "aiaesthetic") {
                                        showScoreCheck.checked = s.showScore !== undefined ? s.showScore : true
                                        colorScoreCheck.checked = s.colorScore !== undefined ? s.colorScore : true
                                        applyBiasCheck.checked = s.applyUserBias !== undefined ? s.applyUserBias : true
                                    }

                                    stepSettingsPopup.open()
                                }
                                contentItem: Text {
                                    text: "⚙"
                                    font.pixelSize: 13
                                    color: textColor
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: null
                            }
                        }
                    }
                }

                // Separator between preprocessors and processors
                Rectangle {
                    width: parent.width
                    height: 1
                    color: popupBorder
                }

                // ---- PROCESSORS section ----
                Text {
                    text: "Processors"
                    color: isLight ? "#555" : "#bbb"
                    font.pixelSize: 12
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                }
                Text { text: "Drag '≡' to reorder."; color: isLight ? "#666" : "#aaa"; font.pixelSize: 12 }

                ListView {
                    id: pipelineList
                    width: parent.width
                    implicitHeight: contentHeight
                    clip: true
                    spacing: 5
                    model: DelegateModel {
                        id: visualModel
                        model: backend.pipelineModel
                        delegate: DropArea {
                            id: delegateRoot
                            width: ListView.view.width
                            height: 45
                            keys: ["step"]

                            property int visualIndex: DelegateModel.itemsIndex

                            onEntered: (drag) => {
                                let from = drag.source.visualIndex
                                let to = delegateRoot.visualIndex
                                if (from !== to) {
                                    visualModel.items.move(from, to)
                                    backend.pipelineModel.moveStep(from, to)
                                }
                            }

                            Rectangle {
                                id: itemRect
                                width: delegateRoot.width
                                height: 45
                                color: isLight ? "#f0f0f0" : "#2a2a2a"
                                radius: 4
                                border.color: popupBorder

                                anchors {
                                    horizontalCenter: parent.horizontalCenter
                                    verticalCenter: parent.verticalCenter
                                }

                                Drag.active: dragArea.drag.active
                                Drag.source: delegateRoot
                                Drag.keys: ["step"]

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8

                                    Text {
                                        text: "≡"
                                        color: isLight ? "#888" : "#666"
                                        font.pixelSize: 18
                                        Layout.alignment: Qt.AlignVCenter
                                    }

                                    CheckBox {
                                        checked: model.enabled
                                        onToggled: backend.pipelineModel.setStepEnabled(model.index, checked)
                                    }

                                    Text {
                                        text: model.name
                                        color: model.enabled ? textColor : (isLight ? "#999" : "#666")
                                        Layout.fillWidth: true
                                        font.pixelSize: 13
                                        elide: Text.ElideRight
                                    }

                                    // Gear icon for step settings
                                    Button {
                                        Layout.preferredWidth: 22
                                        Layout.preferredHeight: 22
                                        flat: true
                                        hoverEnabled: true
                                        opacity: parent.hovered ? 0.8 : 0.35
                                        Behavior on opacity { NumberAnimation { duration: 120 } }
                                        onClicked: {
                                            stepSettingsPopup.stepId = model.id
                                            stepSettingsPopup.stepName = model.name
                                            stepSettingsPopup.stepModel = backend.pipelineModel
                                            stepSettingsPopup.stepIndex = model.index

                                            var s = stepSettingsPopup.stepModel.getStepSettings(stepSettingsPopup.stepIndex) || ({})
                                            if (model.id === "exposure") exposureSlider.value = s.clipThreshold !== undefined ? s.clipThreshold : 0.15
                                            if (model.id === "laplacian") laplacianSlider.value = s.focusThreshold !== undefined ? s.focusThreshold : 150.0
                                            if (model.id === "aiaesthetic") {
                                                showScoreCheck.checked = s.showScore !== undefined ? s.showScore : true
                                                colorScoreCheck.checked = s.colorScore !== undefined ? s.colorScore : true
                                                applyBiasCheck.checked = s.applyUserBias !== undefined ? s.applyUserBias : true
                                            }

                                            stepSettingsPopup.open()
                                        }
                                        contentItem: Text {
                                            text: "⚙"
                                            font.pixelSize: 13
                                            color: textColor
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        background: null
                                    }
                                }

                                // Drag handle covers the card — but NOT the gear button on the right
                                MouseArea {
                                    id: dragArea
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    anchors.rightMargin: 30
                                    propagateComposedEvents: true
                                    drag.target: itemRect
                                    drag.axis: Drag.YAxis
                                    drag.threshold: 6
                                    onClicked: mouse.accepted = false
                                    onPositionChanged: function(mouse) {
                                        if (drag.active)
                                            itemRect.y = Math.max(0, Math.min(itemRect.y, pipelineList.height - itemRect.height))
                                    }
                                }

                                states: [
                                    State {
                                        when: dragArea.drag.active
                                        ParentChange { target: itemRect; parent: pipelineList }
                                        AnchorChanges {
                                            target: itemRect
                                            anchors.horizontalCenter: undefined
                                            anchors.verticalCenter: undefined
                                        }
                                        PropertyChanges {
                                            target: itemRect
                                            width: pipelineList.width
                                            height: 45
                                            opacity: 0.8
                                            z: 10
                                        }
                                    }
                                ]
                            }
                        }
                    }
                }

                // Separator between processors and postprocessors
                Rectangle {
                    width: parent.width
                    height: 1
                    color: popupBorder
                }

                // ---- POSTPROCESSORS section ----
                Text {
                    text: "Postprocessors"
                    color: isLight ? "#555" : "#bbb"
                    font.pixelSize: 12
                    font.bold: true
                    font.capitalization: Font.AllUppercase
                    font.letterSpacing: 0.8
                }

                ListView {
                    id: postprocessorList
                    width: parent.width
                    implicitHeight: contentHeight
                    clip: true
                    spacing: 4
                    interactive: false
                    model: backend.postprocessorModel

                    delegate: Rectangle {
                        width: postprocessorList.width
                        height: 40
                        radius: 4
                        color: isLight ? "#f0f0f0" : "#2a2a2a"
                        border.color: popupBorder

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 6
                            spacing: 8

                            CheckBox {
                                enabled: model.supportsDisable
                                checked: model.enabled
                                opacity: model.supportsDisable ? 1.0 : 0.4
                                onToggled: {
                                    if (model.id === "clip_embedding") {
                                        backend.setGroupingMode(checked ? "clip_embedding" : "none")
                                    } else {
                                        backend.postprocessorModel.setStepEnabled(model.index, checked)
                                    }
                                }
                            }

                            Text {
                                text: model.name
                                color: model.enabled ? textColor : (isLight ? "#999" : "#666")
                                Layout.fillWidth: true
                                font.pixelSize: 13
                                elide: Text.ElideRight
                            }

                            // Gear icon for step settings
                            Button {
                                Layout.preferredWidth: 22
                                Layout.preferredHeight: 22
                                flat: true
                                hoverEnabled: true
                                opacity: parent.hovered ? 0.8 : 0.35
                                Behavior on opacity { NumberAnimation { duration: 120 } }
                                onClicked: {
                                    stepSettingsPopup.stepId = model.id
                                    stepSettingsPopup.stepName = model.name
                                    stepSettingsPopup.stepModel = backend.postprocessorModel
                                    stepSettingsPopup.stepIndex = model.index

                                    var s = stepSettingsPopup.stepModel.getStepSettings(stepSettingsPopup.stepIndex) || ({})
                                    if (model.id === "exposure") exposureSlider.value = s.clipThreshold !== undefined ? s.clipThreshold : 0.15
                                    if (model.id === "laplacian") laplacianSlider.value = s.focusThreshold !== undefined ? s.focusThreshold : 150.0
                                    if (model.id === "visual_hash") hashThresholdSlider.value = s.hammingThreshold !== undefined ? s.hammingThreshold : 20
                                    if (model.id === "clip_embedding") clipThresholdSlider.value = s.cosineThreshold !== undefined ? s.cosineThreshold : 0.90
                                    if (model.id === "aiaesthetic") {
                                        showScoreCheck.checked = s.showScore !== undefined ? s.showScore : true
                                        colorScoreCheck.checked = s.colorScore !== undefined ? s.colorScore : true
                                        applyBiasCheck.checked = s.applyUserBias !== undefined ? s.applyUserBias : true
                                    }

                                    stepSettingsPopup.open()
                                }
                                contentItem: Text {
                                    text: "⚙"
                                    font.pixelSize: 13
                                    color: textColor
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                }
                                background: null
                            }
                        }
                    }
                }
            } // Column
            } // ScrollView
        } 

                // RIGHT SIDE Main Content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20
            spacing: 10

            // --- TOP ROW: Directory, Scanning, and Settings ---
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                StyledButton {
                    text: mainWindow.pipelineVisible ? "Hide Pipeline" : "Show Pipeline"
                    onClicked: mainWindow.pipelineVisible = !mainWindow.pipelineVisible
                }
                StyledButton {
                    text: "Open Folder"
                    onClicked: folderDialog.open()
                }
                StyledButton {
                    text: "Start Scan"
                    isPrimary: true
                    onClicked: backend.startScan()
                }
                StyledButton {
                    text: "Cancel"
                    onClicked: backend.cancelScan()
                }
StyledButton {
                    text: "Settings"
                    onClicked: settingsPopup.open()
                }
                
                // Toggle photo controls visibility
                Button {
                    width: 34
                    height: 34
                    hoverEnabled: true
                    flat: true
                    onClicked: mainWindow.showPhotoControls = !mainWindow.showPhotoControls
                    
                    contentItem: Text {
                        text: mainWindow.showPhotoControls ? "▼" : "▲"
                        font.pixelSize: 12
                        color: textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered ? (isLight ? "#e0e0e0" : "#333333") : "transparent"
                        border.color: isLight ? "#cccccc" : "#555555"
                        border.width: 1
                        Behavior on color { ColorAnimation { duration: 100 } }
                    }
                    
                    ToolTip.visible: hovered
                    ToolTip.text: mainWindow.showPhotoControls ? "Hide photo controls" : "Show photo controls"
                }

                // Status badge — fills remaining space, shrinks gracefully
                Rectangle {
                    Layout.fillWidth: true
                    readonly property bool isScanning:  backend.statusText === "Scanning..."
                    readonly property bool isFinished:  backend.statusText === "Finished"
                    readonly property bool isCancelled: backend.statusText === "Cancelled"

                    readonly property color accentColor:
                        isScanning  ? "#0078d4" :
                        isFinished  ? "#4caf50" :
                        isCancelled ? "#f44336" :
                                      (isLight ? "#888888" : "#666666")

                    radius: 6
                    height: 34
                    color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.12)
                    border.color: Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.45)
                    border.width: 1
                    clip: true

                    Behavior on color        { ColorAnimation { duration: 200 } }
                    Behavior on border.color { ColorAnimation { duration: 200 } }

                    Text {
                        anchors.centerIn: parent
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        color: parent.accentColor

                        readonly property bool scanning: parent.isScanning
                        readonly property int  pct:
                            backend.totalFiles > 0
                                ? Math.round(backend.progress / backend.totalFiles * 100)
                                : 0

                        text: scanning
                            ? backend.progress + " / " + backend.totalFiles + "  (" + pct + "%)"
                            : backend.statusText

                        Behavior on color { ColorAnimation { duration: 200 } }
                    }
                }
}
            
            // --- Collapsible Photo Controls (Rows 2 & 3) ---
            Rectangle {
                id: photoControlsContainer
                Layout.fillWidth: true
                Layout.preferredHeight: photoControlsColumn.height
                Layout.minimumHeight: 0
                color: "transparent"
                clip: true
                
                Behavior on Layout.preferredHeight { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
                
                Column {
                    id: photoControlsColumn
                    width: parent.width
                    spacing: 10
                    height: mainWindow.showPhotoControls ? photoControlsContent.implicitHeight : 0
                    
                    Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
                    
                    Column {
                    id: photoControlsContent
                    spacing: 10
                    
                    // --- SECOND ROW: Photo Menu Controls (View, Sort, LUT) ---
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 15

                Text {
                    text: "Layout:"
                    color: isLight ? "#666" : "#aaa"
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignVCenter
                }

                StyledComboBox {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    model: ["Mosaic Grid", "Detailed List"]
                    currentIndex: isGridView ? 0 : 1
                    onActivated: isGridView = (currentIndex === 0)
                }

                Rectangle {
                    width: 1
                    height: 20
                    color: popupBorder
                }

                Text {
                    text: "Sort:"
                    color: isLight ? "#666" : "#aaa"
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignVCenter
                }

                StyledComboBox {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    model: ["Default", "Best First", "Worst First"]
                    currentIndex: backend.burstProxy.sortMode
                    onActivated: backend.burstProxy.sortMode = currentIndex
                }

                Rectangle {
                    width: 1
                    height: 20
                    color: popupBorder
                }

                Text {
                    text: "LUT:"
                    color: isLight ? "#666" : "#aaa"
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignVCenter
                }

                StyledComboBox {
                    id: lutCombo
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    property var lutList: backend.availableLuts
                    model: {
                        var list = ["None"]
                        for (var i = 0; i < lutList.length; i++) list.push(lutList[i])
                        return list
                    }
                    currentIndex: {
                        if (!backend.lutEnabled || backend.activeLutName === "none") return 0
                        var idx = lutList.indexOf(backend.activeLutName)
                        return idx >= 0 ? idx + 1 : 0
                    }
                    onActivated: {
                        if (currentIndex === 0) {
                            backend.selectLutPreset("none")
                        } else {
                            backend.selectLutPreset(model[currentIndex])
                        }
                    }
                    Connections {
                        target: backend
                        function onActiveLutChanged() { lutCombo.currentIndex = lutCombo.currentIndex }
                    }
                }

                StyledButton {
                    text: "Load .cube..."
                    Layout.minimumWidth: 0
                    onClicked: lutFileDialog.open()
                }

                FileDialog {
                    id: lutFileDialog
                    title: "Select 3D LUT File"
                    nameFilters: ["CUBE LUT files (*.cube *.CUBE)", "All files (*)"]
                    onAccepted: backend.loadLutFile(selectedFile)
                }
            }

            // --- COLOR LABEL FILTER ROW ---
            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: "Label:"
                    color: isLight ? "#666" : "#aaa"
                    font.pixelSize: 13
                    Layout.alignment: Qt.AlignVCenter
                }

                // "All" button
                Rectangle {
                    width: 36; height: 24; radius: 4
                    color: backend.burstProxy.colorLabelFilter === "" ? (isLight ? "#0066cc" : "#3a80d2") : (isLight ? "#e0e0e0" : "#2a2a2a")
                    border.color: isLight ? "#ccc" : "#444"
                    Layout.alignment: Qt.AlignVCenter
                    Text {
                        anchors.centerIn: parent
                        text: "All"
                        color: backend.burstProxy.colorLabelFilter === "" ? "white" : (isLight ? "#333" : "#aaa")
                        font.pixelSize: 12
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: backend.burstProxy.colorLabelFilter = "" }
                }

                // Colored dot buttons
                Repeater {
                    model: [
                        { label: "Red",    color: "#e05050" },
                        { label: "Yellow", color: "#d4b800" },
                        { label: "Green",  color: "#3caa3c" },
                        { label: "Blue",   color: "#3a80d2" },
                        { label: "Purple", color: "#9b59b6" }
                    ]
                    delegate: Rectangle {
                        property bool active: backend.burstProxy.colorLabelFilter === modelData.label
                        width: 24; height: 24; radius: 12
                        color: modelData.color
                        border.color: active ? "white" : "transparent"
                        border.width: active ? 2 : 0
                        opacity: active ? 1.0 : 0.55
                        Layout.alignment: Qt.AlignVCenter
                        ToolTip.visible: hoverArea.containsMouse
                        ToolTip.text: modelData.label
                        MouseArea {
                            id: hoverArea
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: backend.burstProxy.colorLabelFilter =
                                parent.active ? "" : modelData.label
                        }
                    }
                }

                Item { Layout.fillWidth: true }
            }
        } // End of photoControlsContent Column
    } // End of photoControlsColumn Column
} // End of photoControlsContainer Rectangle

            // Selection action bar — visible when items are selected
            Rectangle {
                Layout.fillWidth: true
                height: 40
                visible: mainWindow.selectedCount > 0
                color: isLight ? "#e3f0ff" : "#003366"
                radius: 6
                border.color: isLight ? "#99c2ff" : "#0055bb"
                border.width: 1

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    Text {
                        text: mainWindow.selectedCount + " image" + (mainWindow.selectedCount !== 1 ? "s" : "") + " selected"
                        color: isLight ? "#003399" : "#99ccff"
                        font.pixelSize: 13
                        font.weight: Font.Medium
                        Layout.fillWidth: true
                    }

                    Button {
                        id: deleteSelectedBtn
                        text: "Move to Trash"
                        hoverEnabled: true
                        onClicked: trashMultiConfirmDialog.openForSelection()

                        contentItem: Text {
                            text: deleteSelectedBtn.text
                            font.pixelSize: 13
                            font.weight: Font.Bold
                            color: "#ffffff"
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        background: Rectangle {
                            implicitWidth: deleteSelectedBtn.contentItem.implicitWidth + 30
                            implicitHeight: 28
                            radius: 5
                            color: deleteSelectedBtn.down ? "#b71c1c" : (deleteSelectedBtn.hovered ? "#e53935" : "#c62828")
                            border.color: "#b71c1c"
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: 100 } }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            acceptedButtons: Qt.NoButton
                        }
                    }

                    StyledButton {
                        text: "Deselect All"
                        onClicked: mainWindow.clearSelection()
                    }
                }
            }

            // 3-segment quality bar: green (accepted) | red (rejected) | grey (unscanned)
            Rectangle {
                Layout.fillWidth: true
                height: 10
                radius: 5
                visible: backend.totalFiles > 0
                color: isLight ? "#dddddd" : "#3a3a3a"  // grey base (unscanned)
                clip: true

                readonly property real total: backend.totalFiles > 0 ? backend.totalFiles : 1
                readonly property real acceptedFrac: backend.acceptedCount / total
                readonly property real rejectedFrac: backend.rejectedCount / total

                // Green segment (accepted) — left-anchored
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: parent.width * parent.acceptedFrac
                    color: "#4caf50"
                    Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
                }

                // Red segment (rejected) — immediately after green
                Rectangle {
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    x: parent.width * parent.acceptedFrac
                    width: parent.width * parent.rejectedFrac
                    color: "#f44336"
                    Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
                    Behavior on x    { NumberAnimation { duration: 120; easing.type: Easing.OutQuad } }
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    anchors.fill: parent
                    model: backend.burstProxy
                    clip: true
                    cellWidth: 220
                    cellHeight: 260
                    visible: isGridView

                    ScrollBar.vertical: ScrollBar {
                        active: true
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            implicitWidth: 8
                            radius: 4
                            color: isLight ? "#999999" : "#666666"
                        }
                    }

                    delegate: Item {
                        width: 220  // Matches cellWidth
                        height: 260 // Matches cellHeight

                        // --- THE SEAMLESS BATCH FRAME ---
                        Rectangle {
                            anchors.fill: parent
                            // A soft blue background to group them
                            color: isLight ? "#d0e8ff" : "#004191" 
                            visible: mainWindow.groupBursts && model.isExpanded && model.groupCount > 1
                        }

                        // --- THE ACTUAL CARD ---
                        Rectangle {
                            id: gridCard
                            width: 200
                            height: 240
                            anchors.centerIn: parent // Centers the card inside the 220x260 cell

                            readonly property bool itemSelected: !!mainWindow.selectedPaths[model.filePath]

                            color: model.status === "WAITING" ? cardWaitingBg : (model.isRejected ? cardBlurryBg : cardNeutralBg)
                            radius: 6
                            border.color: (model.isBestShot && mainWindow.groupBursts && model.isExpanded) ? "#FFD700" : (itemSelected ? "#0066cc"
                                        : (model.status === "WAITING" ? cardWaitingBorder : (model.isRejected ? cardBlurryBorder : cardNeutralBorder)))
                            border.width: (model.isBestShot && mainWindow.groupBursts && model.isExpanded) ? 3 : (itemSelected ? 3 : 1)

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: function(mouse) {
                                    if (mouse.modifiers & Qt.ControlModifier) {
                                        mainWindow.toggleOne(model.filePath, index)
                                    } else if (mouse.modifiers & Qt.ShiftModifier) {
                                        mainWindow.selectRange(index)
                                    } else if (gridCard.itemSelected) {
                                        mainWindow.toggleOne(model.filePath, index)
                                    } else {
                                        mainWindow.selectSingle(model.filePath, index)
                                    }
                                }
                                onDoubleClicked: {
                                    mainWindow.clearSelection()
                                    viewerWindow.currentIndex = index
                                    viewerWindow.show()
                                }
                            }

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 5

                                Image {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 160
                                    source: "image://preview/" + model.filePath
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit

                                    Rectangle {
                                        anchors.fill: parent
                                        color: "black"
                                        z: -1
                                    }
                                }

                                Text {
                                    text: model.fileName
                                    color: textColor
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: {
                                        if (model.status === "WAITING") return "Waiting...";
                                        if (model.isRejected) return "Rejected: " + model.rejectReason;
                                        if (!backend.showAiScore) return "";
                                        return "Score: " + model.score;
                                    }
                                    color: model.status === "WAITING" ? cardWaitingText : (model.isRejected ? cardBlurryText : (backend.colorAiScore ? getScoreColor(parseFloat(model.score)) : textColor))
                                    font.bold: true
                                    font.pixelSize: 12
                                }
                            }

                            // COLOR LABEL BOOKMARK — left edge of card
                            Rectangle {
                                visible: model.colorLabel !== ""
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.topMargin: 24
                                width: 6
                                height: 36
                                radius: 3
                                color: {
                                    if (model.colorLabel === "Red")    return "#e05050"
                                    if (model.colorLabel === "Yellow") return "#d4b800"
                                    if (model.colorLabel === "Green")  return "#3caa3c"
                                    if (model.colorLabel === "Blue")   return "#3a80d2"
                                    if (model.colorLabel === "Purple") return "#9b59b6"
                                    return "transparent"
                                }
                                z: 11
                            }

                            // BADGE
                            Rectangle {
                                visible: mainWindow.groupBursts && model.isLead && model.groupCount > 1
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 8
                                width: 32
                                height: 32
                                radius: 16
                                color: "#0066cc"
                                border.color: "#ffffff"
                                border.width: 2
                                z: 10

                                Text {
                                    anchors.centerIn: parent
                                    text: model.isExpanded ? "<" : ("+" + (model.groupCount - 1))
                                    color: "white"
                                    font.bold: true
                                    font.pixelSize: 14
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    preventStealing: true
                                    propagateComposedEvents: false
                                    onClicked: mainWindow.toggleGroupExpansion(index)
                                }
                            }

                            // CHILD RINGS
                            Rectangle {
                                visible: mainWindow.groupBursts && !model.isLead
                                anchors.top: parent.top
                                anchors.left: parent.left
                                anchors.margins: 8
                                width: 20
                                height: 20
                                radius: 10
                                color: "transparent"
                                border.color: isLight ? "#888888" : "#aaaaaa"
                                border.width: 2
                                z: 10
                            }
                        }
                    }
                }

                ListView {
                    anchors.fill: parent
                    model: backend.burstProxy
                    clip: true
                    spacing: 4
                    visible: !isGridView

                    ScrollBar.vertical: ScrollBar {
                        active: true
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle {
                            implicitWidth: 8
                            radius: 4
                            color: isLight ? "#999999" : "#666666"
                        }
                    }

                                        delegate: Item {
                        width: ListView.view.width
                        height: 68 // 60 for the card + 8 for spacing

                        // --- THE SEAMLESS BATCH FRAME ---
                        Rectangle {
                            anchors.fill: parent
                            color: isLight ? "#d0e8ff" : "#004191"
                            visible: mainWindow.groupBursts && model.isExpanded && model.groupCount > 1
                        }

                        // --- THE ACTUAL CARD ---
                        Rectangle {
                            id: listCard
                            width: parent.width - 16
                            height: 60
                            anchors.centerIn: parent

                            readonly property bool itemSelected: !!mainWindow.selectedPaths[model.filePath]

                            color: model.status === "WAITING" ? cardWaitingBg : (model.isRejected ? cardBlurryBg : cardNeutralBg)
                            radius: 4
                            border.color: (model.isBestShot && mainWindow.groupBursts && model.isExpanded) ? "#FFD700" : (itemSelected ? "#0066cc"
                                        : (model.status === "WAITING" ? cardWaitingBorder : (model.isRejected ? cardBlurryBorder : cardNeutralBorder)))
                            border.width: (model.isBestShot && mainWindow.groupBursts && model.isExpanded) ? 3 : (itemSelected ? 3 : 1)

                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.LeftButton
                                onClicked: function(mouse) {
                                    if (mouse.modifiers & Qt.ControlModifier) {
                                        mainWindow.toggleOne(model.filePath, index)
                                    } else if (mouse.modifiers & Qt.ShiftModifier) {
                                        mainWindow.selectRange(index)
                                    } else if (listCard.itemSelected) {
                                        mainWindow.toggleOne(model.filePath, index)
                                    } else {
                                        mainWindow.selectSingle(model.filePath, index)
                                    }
                                }
                                onDoubleClicked: {
                                    mainWindow.clearSelection()
                                    viewerWindow.currentIndex = index
                                    viewerWindow.show()
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 15

                                Image {
                                    Layout.preferredWidth: 70
                                    Layout.preferredHeight: 50
                                    source: "image://preview/" + model.filePath
                                    asynchronous: true
                                    fillMode: Image.PreserveAspectFit

                                    Rectangle {
                                        anchors.fill: parent
                                        color: "black"
                                        z: -1
                                    }
                                }

                                Text {
                                    text: model.fileName
                                    color: textColor
                                    font.pixelSize: 14
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }

                                Text {
                                    text: {
                                        if (model.status === "WAITING") return "Waiting...";
                                        if (model.isRejected) return "Rejected: " + model.rejectReason;
                                        if (!backend.showAiScore) return "";
                                        return "Score: " + model.score;
                                    }
                                    color: model.status === "WAITING" ? cardWaitingText : (model.isRejected ? cardBlurryText : (backend.colorAiScore ? getScoreColor(parseFloat(model.score)) : textColor))
                                    font.bold: true
                                    font.pixelSize: 14
                                    Layout.alignment: Qt.AlignRight
                                    Layout.rightMargin: 10
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}