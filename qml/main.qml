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
    
    property color cardSharpBg: isLight ? "#eaffe8" : "#1c4a2a"
    property color cardSharpBorder: isLight ? "#66cc66" : "#44ff44"
    property color cardSharpText: isLight ? "#008800" : "#88ff88"

    property bool isGridView: true
    property bool pipelineVisible: false // Property for the left sidebar
    property bool groupBursts: backend.groupBursts

    function toggleGroupExpansion(leadIndex) {
            let isExpanded = !resultsModel.get(leadIndex).isExpanded
            resultsModel.setProperty(leadIndex, "isExpanded", isExpanded)
        
            for (let i = leadIndex + 1; i < resultsModel.count; i++) {
                if (resultsModel.get(i).isLead) break;
                resultsModel.setProperty(i, "isExpanded", isExpanded)
            }
        }

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
                isExpanded: false
            })
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
    }

    FolderDialog {
        id: folderDialog
        onAccepted: { resultsModel.clear(); backend.selectFolder(folderDialog.selectedFolder) }
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

                Text { text: "Write EXIF rating:"; color: textColor }
                CheckBox { checked: backend.writeExif; onCheckedChanged: backend.writeExif = checked }

                Text { text: "Cache Laplacian data:"; color: textColor }
                CheckBox { checked: backend.cacheLaplacian; onCheckedChanged: backend.cacheLaplacian = checked }

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
            }
            Item { Layout.fillHeight: true }
            StyledButton { text: "Close"; Layout.alignment: Qt.AlignHCenter; onClicked: settingsPopup.close() }
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

        onCurrentIndexChanged: {
            if (currentIndex >= 0 && currentIndex < resultsModel.count) {
                const file = resultsModel.get(currentIndex).filePath
                
                viewerWindow.currentFilePath = file;
                
                viewerImage.scale = 1.0
                flickable.contentX = 0
                flickable.contentY = 0
                
                viewerImage.source = ""
                viewerImage.source = "image://full/" + encodeURIComponent(file)
                title = "Viewer - " + resultsModel.get(currentIndex).fileName
                
                let meta = backend.getPhotoMetadata(encodeURIComponent(file));
                exifInfo = meta.infoText;
                
                viewerWindow.currentRating = backend.getPhotoRating(file);
                
                console.log("[QML] Photo changed. New Path:", viewerWindow.currentFilePath, "Rating:", viewerWindow.currentRating);
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
                                    
                                    text: starVal <= viewerWindow.currentRating ? "★" : "☆"
                                    color: starVal <= viewerWindow.currentRating ? "#FFD700" : "#aaaaaa"
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
                        onClicked: if (viewerWindow.currentIndex < resultsModel.count - 1) viewerWindow.currentIndex++
                        enabled: viewerWindow.currentIndex < resultsModel.count - 1
                    }
                }

                function setRatingFromKey(rating) {
                    console.log("[QML] Key pressed:", rating);
                    if (viewerWindow.currentRating === rating) {
                        viewerWindow.currentRating = 0;
                    } else {
                        viewerWindow.currentRating = rating;
                    }
                        backend.setPhotoRating(viewerWindow.currentFilePath, viewerWindow.currentRating);
                }   

                Shortcut { sequence: "1"; onActivated: viewerWindow.setRatingFromKey(1) }
                Shortcut { sequence: "2"; onActivated: viewerWindow.setRatingFromKey(2) }
                Shortcut { sequence: "3"; onActivated: viewerWindow.setRatingFromKey(3) }
                Shortcut { sequence: "4"; onActivated: viewerWindow.setRatingFromKey(4) }
                Shortcut { sequence: "5"; onActivated: viewerWindow.setRatingFromKey(5) }
                Shortcut { sequence: "0"; onActivated: viewerWindow.setRatingFromKey(0) }
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
        Shortcut { sequence: "Right"; onActivated: if (viewerWindow.currentIndex < resultsModel.count - 1) viewerWindow.currentIndex++ }
        Shortcut { sequence: "Escape"; onActivated: viewerWindow.close() }
        Shortcut { 
            sequence: "Delete" 
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

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 15
                spacing: 15
                visible: pipelineSidebar.Layout.preferredWidth > 50

                Text { text: "Pipeline Config"; color: textColor; font.bold: true; font.pixelSize: 16 }
                Text { text: "Drag '≡' to reorder."; color: isLight ? "#666" : "#aaa"; font.pixelSize: 12 }

                ListView {
                    id: pipelineList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
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
                                width: parent.width
                                height: parent.height
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

                                    Item {
                                        width: 24
                                        height: parent.height
                                        Text {
                                            anchors.centerIn: parent
                                            text: "≡"
                                            color: isLight ? "#888" : "#666"
                                            font.pixelSize: 18
                                        }
                                        MouseArea {
                                            id: dragArea
                                            anchors.fill: parent
                                            drag.target: itemRect
                                            drag.axis: Drag.YAxis
                                        }
                                    }

                                    CheckBox {
                                        checked: model.enabled
                                        onCheckedChanged: {
                                            backend.pipelineModel.setStepEnabled(model.index, checked)
                                        }
                                    }

                                    Text {
                                        text: model.name
                                        color: model.enabled ? textColor : (isLight ? "#999" : "#666")
                                        Layout.fillWidth: true
                                        font.pixelSize: 13
                                    }
                                }

                                states: [
                                    State {
                                        when: dragArea.drag.active
                                        ParentChange { target: itemRect; parent: pipelineList }
                                        AnchorChanges { target: itemRect; anchors.horizontalCenter: undefined; anchors.verticalCenter: undefined }
                                        PropertyChanges { target: itemRect; opacity: 0.8; z: 10 }
                                    }
                                ]
                            }
                        }
                    } 
                } 

                
                Item { 
                    Layout.fillHeight: true 
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: popupBorder
                }

                CheckBox {
                    text: "Group Burst Photos"
                    checked: mainWindow.groupBursts
                    onCheckedChanged: {
                        mainWindow.groupBursts = checked
                        backend.groupBursts = checked
                    }
                    contentItem: Text {
                        text: parent.text
                        color: textColor
                        font.pixelSize: 14
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: parent.indicator.width + parent.spacing
                    }
                }
            }
        } 

                // RIGHT SIDE Main Content
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            anchors.margins: 20
            spacing: 15

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
                Rectangle {
                    width: 1
                    height: 20
                    color: popupBorder
                    Layout.margins: 5
                }
                StyledButton {
                    text: isGridView ? "View Mosaic" : "View List"
                    onClicked: isGridView = !isGridView
                }
                StyledButton {
                    text: "Settings"
                    onClicked: settingsPopup.open()
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: backend.statusText
                    color: textColor
                    font.pixelSize: 16
                }
            }

            ProgressBar {
                Layout.fillWidth: true
                from: 0
                to: backend.totalFiles
                value: backend.progress
                visible: backend.totalFiles > 0
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                GridView {
                    anchors.fill: parent
                    model: resultsModel
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

                    delegate: Rectangle {
                        property bool shouldShow: !mainWindow.groupBursts || model.isLead || model.isExpanded
                        width: shouldShow ? 200 : 0
                        height: shouldShow ? 240 : 0
                        visible: shouldShow
                        color: model.status === "WAITING" ? cardWaitingBg : (model.isRejected ? cardBlurryBg : cardSharpBg)
                        radius: 6
                        border.color: model.status === "WAITING" ? cardWaitingBorder : (model.isRejected ? cardBlurryBorder : cardSharpBorder)
                        border.width: 1

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
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
                                    return "Score: " + model.score;
                                }
                                color: model.status === "WAITING" ? cardWaitingText : (model.isRejected ? cardBlurryText : cardSharpText)
                                font.bold: true
                                font.pixelSize: 12
                            }
                        }

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
                                // Show "<" if expanded, otherwise "+X"
                                text: model.isExpanded ? "<" : ("+" + (model.groupCount - 1))
                                color: "white"
                                font.bold: true
                                font.pixelSize: 14
                            }
                            
                            // Make it clickable!
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    mainWindow.toggleGroupExpansion(index)
                                }
                            }
                        }

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

                ListView {
                    anchors.fill: parent
                    model: resultsModel
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

                    delegate: Rectangle {
                        property bool shouldShow: !mainWindow.groupBursts || model.isLead || model.isExpanded
                        width: ListView.view.width
                        height: shouldShow ? 60 : 0
                        visible: shouldShow
                        color: model.status === "WAITING" ? cardWaitingBg : (model.isRejected ? cardBlurryBg : cardSharpBg)
                        radius: 4
                        border.color: model.status === "WAITING" ? cardWaitingBorder : (model.isRejected ? cardBlurryBorder : cardSharpBorder)
                        border.width: 1

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
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
                                    return "Score: " + model.score;
                                }
                                color: model.status === "WAITING" ? cardWaitingText : (model.isRejected ? cardBlurryText : cardSharpText)
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