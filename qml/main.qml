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
    color: "#1e1e1e"

    property bool isGridView: true

    ListModel { id: resultsModel }

    Connections {
        target: backend
        
        function onFileFound(fileName, filePath, index) {
            resultsModel.append({
                "fileName": fileName,
                "filePath": filePath,
                "isBlurry": false,
                "score": "0.00",
                "status": "WAITING"
            })
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
        onAccepted: {
            resultsModel.clear()
            backend.selectFolder(folderDialog.selectedFolder)
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
            
            Rectangle { width: 1; height: 20; color: "#555"; Layout.margins: 5 }
            
            Button { 
                text: isGridView ? "View: Mosaic" : "View: List" 
                icon.name: isGridView ? "view-grid" : "view-list"
                onClicked: isGridView = !isGridView
            }
            
            Item { Layout.fillWidth: true } 
            
            Text { text: backend.statusText; color: "white"; font.pixelSize: 16 }
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

                delegate: Rectangle {
                    width: 200
                    height: 240
                    color: model.status === "WAITING" ? "#2a2a2a" : (model.isBlurry ? "#4a1c1c" : "#1c4a2a")
                    radius: 6
                    border.color: model.status === "WAITING" ? "#444444" : (model.isBlurry ? "#ff4444" : "#44ff44")
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

                        Text { 
                            text: model.fileName
                            color: "white"
                            font.pixelSize: 12
                            Layout.fillWidth: true
                            elide: Text.ElideRight 
                        }

                        Text { 
                            text: model.status === "WAITING" ? "Waiting..." : (model.status + " (" + model.score + ")")
                            color: model.status === "WAITING" ? "#aaaaaa" : (model.isBlurry ? "#ff8888" : "#88ff88")
                            font.bold: true
                            font.pixelSize: 12
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
                    width: ListView.view.width
                    height: 60
                    color: model.status === "WAITING" ? "#2a2a2a" : (model.isBlurry ? "#4a1c1c" : "#1c4a2a")
                    radius: 4
                    border.color: model.status === "WAITING" ? "#444444" : (model.isBlurry ? "#ff4444" : "#44ff44")
                    border.width: 1

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
                            Rectangle { anchors.fill: parent; color: "black"; z: -1 }
                        }

                        Text { 
                            text: model.fileName
                            color: "white"
                            font.pixelSize: 14
                            Layout.fillWidth: true
                            elide: Text.ElideRight 
                        }

                        Text { 
                            text: model.status === "WAITING" ? "Waiting for scan..." : (model.status + " (" + model.score + ")")
                            color: model.status === "WAITING" ? "#aaaaaa" : (model.isBlurry ? "#ff8888" : "#88ff88")
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