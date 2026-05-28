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

    ListModel { id: resultsModel }

    Connections {
        target: backend
        function onFileFound(fileName, filePath, index) {
            resultsModel.append({ "fileName": fileName, "filePath": filePath, "isBlurry": false, "score": "0.00", "status": "WAITING" })
        }
        function onFileProcessed(index, isBlurry, aestheticScore, width, height) {
            if (index >= 0 && index < resultsModel.count) {
                resultsModel.setProperty(index, "isBlurry", isBlurry)
                resultsModel.setProperty(index, "score", aestheticScore.toFixed(2))
                resultsModel.setProperty(index, "status", isBlurry ? "BLURRY" : "SHARP")
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
            Button { text: "Close"; Layout.alignment: Qt.AlignHCenter; onClicked: settingsPopup.close() }
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
                        // Keep index within bounds after deletion
                        if (targetIndex >= resultsModel.count) {
                            viewerWindow.currentIndex = resultsModel.count - 1;
                        } else {
                            // Force UI refresh for the same index
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

        onCurrentIndexChanged: {
            if (currentIndex >= 0 && currentIndex < resultsModel.count) {
                const file = resultsModel.get(currentIndex).filePath
                
                viewerImage.scale = 1.0
                flickable.contentX = 0
                flickable.contentY = 0
                
                viewerImage.source = ""
                viewerImage.source = "image://full/" + encodeURIComponent(file)
                title = "Viewer - " + resultsModel.get(currentIndex).fileName
                
                // Fetch EXIF metadata from C++
                let meta = backend.getPhotoMetadata(encodeURIComponent(file));
                exifInfo = meta.infoText;
            }
        }

        RowLayout {
            anchors.fill: parent
            spacing: 0

            // Left side: The Image Viewer
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

                // Bottom Navigation Bar overlay
                RowLayout {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.margins: 30
                    spacing: 20

                    Button {
                        text: "<- Previous"
                        onClicked: if (viewerWindow.currentIndex > 0) viewerWindow.currentIndex--
                        enabled: viewerWindow.currentIndex > 0
                    }
                    
                    Button {
                        text: "Trash Photo"
                        icon.name: "user-trash"
                        onClicked: {
                            trashConfirmDialog.targetIndex = viewerWindow.currentIndex;
                            trashConfirmDialog.open();
                        }
                    }

                    Button {
                        text: "Next ->"
                        onClicked: if (viewerWindow.currentIndex < resultsModel.count - 1) viewerWindow.currentIndex++
                        enabled: viewerWindow.currentIndex < resultsModel.count - 1
                    }
                }
            }

            // Right side: Metadata Sidebar
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
                    visible: sidebar.Layout.preferredWidth > 100 // Hide content during animation

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
                    }
                }
            }
        }

        // Toggle Sidebar Button (Top Right)
        Button {
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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            
            Button { text: "Open Folder"; onClicked: folderDialog.open() }
            Button { text: "Start Scan"; onClicked: backend.startScan() }
            Button { text: "Cancel"; onClicked: backend.cancelScan() }
            
            Rectangle { width: 1; height: 20; color: popupBorder; Layout.margins: 5 }
            
            Button { 
                text: isGridView ? "View: Mosaic" : "View: List" 
                onClicked: isGridView = !isGridView
            }
            
            Button { 
                text: "Settings"
                onClicked: settingsPopup.open() 
            }
            
            Item { Layout.fillWidth: true } 
            Text { text: backend.statusText; color: textColor; font.pixelSize: 16 }
        }

        ProgressBar {
            Layout.fillWidth: true
            from: 0; to: backend.totalFiles; value: backend.progress
            visible: backend.totalFiles > 0
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            GridView {
                anchors.fill: parent
                model: resultsModel
                clip: true
                cellWidth: 220; cellHeight: 260
                visible: isGridView

                delegate: Rectangle {
                    width: 200; height: 240
                    color: model.status === "WAITING" ? cardWaitingBg : (model.isBlurry ? cardBlurryBg : cardSharpBg)
                    radius: 6
                    border.color: model.status === "WAITING" ? cardWaitingBorder : (model.isBlurry ? cardBlurryBorder : cardSharpBorder)
                    border.width: 1

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            viewerWindow.currentIndex = index;
                            viewerWindow.show();
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
                            Rectangle { anchors.fill: parent; color: "black"; z: -1 }
                        }

                        Text { text: model.fileName; color: textColor; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }

                        Text { 
                            text: model.status === "WAITING" ? "Waiting..." : (model.status + " (" + model.score + ")")
                            color: model.status === "WAITING" ? cardWaitingText : (model.isBlurry ? cardBlurryText : cardSharpText)
                            font.bold: true; font.pixelSize: 12
                        }
                    }
                }
            }

            ListView {
                anchors.fill: parent
                model: resultsModel
                clip: true
                spacing: 4
                visible: !isGridView

                delegate: Rectangle {
                    width: ListView.view.width; height: 60
                    color: model.status === "WAITING" ? cardWaitingBg : (model.isBlurry ? cardBlurryBg : cardSharpBg)
                    radius: 4
                    border.color: model.status === "WAITING" ? cardWaitingBorder : (model.isBlurry ? cardBlurryBorder : cardSharpBorder)
                    border.width: 1

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            viewerWindow.currentIndex = index;
                            viewerWindow.show();
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 5
                        spacing: 15

                        Image {
                            Layout.preferredWidth: 70; Layout.preferredHeight: 50
                            source: "image://preview/" + model.filePath
                            asynchronous: true; fillMode: Image.PreserveAspectFit
                            Rectangle { anchors.fill: parent; color: "black"; z: -1 }
                        }

                        Text { text: model.fileName; color: textColor; font.pixelSize: 14; Layout.fillWidth: true; elide: Text.ElideRight }

                        Text { 
                            text: model.status === "WAITING" ? "Waiting for scan..." : (model.status + " (" + model.score + ")")
                            color: model.status === "WAITING" ? cardWaitingText : (model.isBlurry ? cardBlurryText : cardSharpText)
                            font.bold: true; font.pixelSize: 14
                            Layout.alignment: Qt.AlignRight; Layout.rightMargin: 10
                        }
                    }
                }
            }
        }
    }
}