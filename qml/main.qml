import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Window {
    id: mainWindow
    width: 900
    height: 700
    visible: true
    title: "SharpMark - AI Image Culling"
    color: "#1e1e1e"

    // Model to store processed images
    ListModel { id: resultsModel }

    // Connect to C++ signals
    Connections {
        target: backend
        
        function onFileProcessed(fileName, filePath, isBlurry, aestheticScore, width, height) {
            resultsModel.append({
                "fileName": fileName,
                "filePath": filePath,
                "isBlurry": isBlurry,
                "score": aestheticScore.toFixed(2)
            })
        }
    }

    FolderDialog {
        id: folderDialog
        onAccepted: backend.selectFolder(folderDialog.selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 15

        // Header controls
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Open Folder"; onClicked: folderDialog.open() }
            Button { text: "Start Scan"; onClicked: { resultsModel.clear(); backend.startScan(); } }
            Button { text: "Cancel"; onClicked: backend.cancelScan() }
            
            Item { Layout.fillWidth: true } // Spacer
            
            Text {
                text: backend.statusText
                color: "white"
                font.pixelSize: 18
            }
        }

        // Progress Bar
        ProgressBar {
            Layout.fillWidth: true
            from: 0
            to: backend.totalFiles
            value: backend.progress
            visible: backend.totalFiles > 0
        }

        // Fast GPU-accelerated list of results
                GridView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: resultsModel
            clip: true
            
            // Cell size for the grid
            cellWidth: 220
            cellHeight: 260

            delegate: Rectangle {
                width: 200
                height: 240
                color: model.isBlurry ? "#4a1c1c" : "#1c4a2a"
                radius: 6
                border.color: model.isBlurry ? "#ff4444" : "#44ff44"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 5

                    Image {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 160
                        
                        // MAGIC HAPPENS HERE: Calls our C++ ThumbnailProvider
                        source: "image://preview/" + model.filePath
                        
                        // Async loading so the GUI never freezes while C++ reads files
                        asynchronous: true 
                        
                        fillMode: Image.PreserveAspectCrop
                        clip: true
                    }

                    Text { 
                        text: model.fileName
                        color: "white"
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        elide: Text.ElideRight // Добавляет "..." если имя слишком длинное
                    }

                    Text { 
                        text: model.isBlurry ? "BLURRY (Score: " + model.score + ")" : "SHARP (Score: " + model.score + ")"
                        color: model.isBlurry ? "#ff8888" : "#88ff88"
                        font.bold: true
                        font.pixelSize: 12
                    }
                }
            }
        }
    }
}