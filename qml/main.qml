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
    
    // --- Dynamic Color Palette ---
    // themeMode: 0 = System (defaults to Dark here), 1 = Light, 2 = Dark
    property bool isLight: backend.themeMode === 1
    
    color: isLight ? "#f5f5f5" : "#1e1e1e"
    property color textColor: isLight ? "#111111" : "#ffffff"
    property color popupBg: isLight ? "#ffffff" : "#2d2d2d"
    property color popupBorder: isLight ? "#cccccc" : "#555555"
    
    // Card Colors
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

    // --- Settings Popup ---
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

            // GRID VIEW
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

            // LIST VIEW
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