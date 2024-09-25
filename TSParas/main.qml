import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "."

ApplicationWindow {
    visible: true
    width: 800
    height: 600
    title: "转子热应力"

    //    flags: Qt.FramelessWindowHint
    property bool showRowLayout: false
    property string ip: "localhost"
    property string hash: "TS1:Mechanism:RotorParams"
    property int maxPages: 1
    property int currentPage: 1
    property var textInputObjs: ({})
    property var textInputs: ({})
    property var parameters: ["slaveID", "controlWord", "density", "radius", "holeRadius", "deltaR", "scanCycle", "surfaceFactor", "centerFactor", "freeFactor"]
    property var listParameters: ["tcz_X", "tcz_Y", "shz_X", "shz_Y", "emz_X", "emz_Y", "prz_X", "prz_Y", "lecz_X", "lecz_Y", "SN1_X", "SN1_Y", "SN2_X", "SN2_Y", "SN3_X", "SN3_Y", "sn"]

    Component {
        id: pageComponent

        Page {
            id: page

            Rectangle {
                width: parent.width
                height: parent.height
                color: "#2E2E2E"
                anchors.fill: parent
                z: -1 // 确保这个 Rectangle 在其他内容的下层
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 0
                anchors.bottomMargin: 50
                anchors.margins: 20

                Rectangle {
                    width: parent.width
                    height: 40
                    color: "#2E2E2E"

                    Text {
                        text: "Thermal Stress"
                        anchors.centerIn: parent
                        font.bold: true
                        color: "#5C6BC0"
                        font.pixelSize: 18
                    }

                    Button {
                        text: "Logout"
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 10
                        font.pixelSize: 14
                        background: Rectangle {
                            color: "#5C6BC0"
                            radius: 5
                        }
                        onClicked: {
                            stackView.pop()
                            showLoginPage()
                        }
                    }
                }

                GridLayout {
                    columns: 3

                    Repeater {
                        id: repeater1
                        model: parameters.length

                        delegate: Row {
                            spacing: 10

                            Text {
                                width: 120
                                text: parameters[index] + ":"
                                color: "#5C6BC0"
                                font.bold: true
                                verticalAlignment: Text.AlignVCenter
                            }

                            Rectangle {
                                width: 80
                                height: 16
                                color: "lightgray"
                                border.color: "white"
                                border.width: 1
                                radius: 5

                                TextInput {
                                    id: textInput1
                                    //                                font.pointSize: 12
                                    color: "black"
                                    z: 1

                                    property string key: parameters[index]

                                    text: index + 1 + (currentPage - 1) * parameters.length

                                    onTextChanged: {
                                        if (!textInputs[currentPage]) {
                                            textInputs[currentPage] = {}
                                        }
                                        textInputs[currentPage][key] = text

                                        if (text.length > 0 && !text.match(
                                                    /^\d*\.?\d*$/)) {
                                            text = text.slice(0,
                                                              text.length - 1)
                                        }
                                    }

                                    Component.onCompleted: {
                                        if (!textInputObjs[currentPage]) {
                                            textInputObjs[currentPage] = {}
                                        }
                                        textInputObjs[currentPage][key] = this
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        textInput1.focus = true
                                    }
                                }
                            }
                        }
                    }
                }

                GridLayout {
                    columns: 1

                    Repeater {
                        id: repeater2
                        model: listParameters.length

                        delegate: Row {
                            spacing: 10

                            Text {
                                width: 60
                                text: listParameters[index] + ":"
                                color: "#5C6BC0"
                                font.bold: true
                                verticalAlignment: Text.AlignVCenter
                            }

                            Rectangle {
                                width: 1400
                                height: 16
                                color: "lightgray"
                                border.color: "white"
                                border.width: 1
                                radius: 5

                                TextInput {
                                    id: textInput2
                                    autoScroll: false
                                    color: "black"
                                    z: 1

                                    property string key: listParameters[index]

                                    text: index + 1 + (currentPage - 1) * listParameters.length

                                    onTextChanged: {
                                        if (!textInputs[currentPage]) {
                                            textInputs[currentPage] = {}
                                        }
                                        textInputs[currentPage][key] = text

                                        if (text.length > 0 && !text.match(
                                                    /^(\s*\d+(\.\d*)?\s*,\s*)*(\s*\d+(\.\d*)?\s*)?$/)) {
                                            text = text.slice(0,
                                                              text.length - 1)
                                        }
                                    }

                                    Component.onCompleted: {
                                        if (!textInputObjs[currentPage]) {
                                            textInputObjs[currentPage] = {}
                                        }
                                        textInputObjs[currentPage][key] = this
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        textInput2.focus = true
                                    }
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    spacing: 10

                    Button {
                        text: "Prev Page"
                        enabled: currentPage > 1
                        width: 100
                        height: 40
                        font.pixelSize: 14
                        background: Rectangle {
                            color: "#5C6BC0"
                            radius: 5
                        }
                        onClicked: {
                            currentPage--
                            updatePage()
                        }
                    }

                    Button {
                        text: "Next Page"
                        enabled: currentPage < maxPages
                        width: 100
                        height: 40
                        font.pixelSize: 14
                        background: Rectangle {
                            color: "#5C6BC0"
                            radius: 5
                        }
                        onClicked: {
                            currentPage++
                            updatePage()
                        }
                    }
                }
            }

            function updatePage() {
                stackView.pop()
                stackView.push(pageComponent)
            }

            Component.onCompleted: {
                fetchData(currentPage)
            }
        }
    }

    StackView {
        id: stackView
        anchors.fill: parent
    }

    Component {
        id: loginComponent
        Login {}
    }

    function handleLoginSuccessful() {
        stackView.pop()
        stackView.push(pageComponent)
        showRowLayout = true
    }

    function showLoginPage() {
        let loginPageInstance = loginComponent.createObject(stackView)
        loginPageInstance.loginSuccessful.connect(handleLoginSuccessful)
        showRowLayout = false
    }

    Component.onCompleted: {
        showLoginPage()
        fetchPageNums()
    }

    RowLayout {
        visible: showRowLayout
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 10
        spacing: 20

        Item {
            width: 200
            height: 16

            Text {
                width: 120
                text: "Rotor Nums:"
                font.bold: true
                color: "#5C6BC0"
                font.pixelSize: 16
                verticalAlignment: Text.AlignVCenter
                anchors.left: parent.left
            }

            Rectangle {
                width: 80
                height: 24
                color: "lightgray"
                border.color: "white"
                border.width: 1
                anchors.right: parent.right

                TextInput {
                    id: maxPagesInput
                    width: parent.width
                    text: maxPages
                    font.pixelSize: 16

                    onTextChanged: {
                        if (text.length > 0 && !text.match(/^[1-9]\d*$/)) {
                            text = text.slice(0, text.length - 1)
                        }
                        let newValue = parseInt(maxPagesInput.text)

                        if (!isNaN(newValue) && newValue !== maxPages) {
                            maxPages = newValue
                            if (currentPage > maxPages) {
                                currentPage = maxPages
                            }
                            stackView.pop()
                            stackView.push(
                                        pageComponent) // Refresh the page content
                        }
                    }
                    z: 1
                }
            }
        }

        Button {
            text: "Save Current TextInputs"
            width: 100
            height: 40
            font.pixelSize: 14
            background: Rectangle {
                color: "#5C6BC0"
                radius: 5
            }
            onClicked: saveCurrentTextInputs()
        }

        Button {
            text: "Load Current TextInputs"
            width: 100
            height: 40
            font.pixelSize: 14
            background: Rectangle {
                color: "#5C6BC0"
                radius: 5
            }
            onClicked: loadCurrentTextInputs()
        }

        Label {
            text: "Page: " + currentPage + "/" + maxPages
            Layout.alignment: Qt.AlignRight
            anchors.bottomMargin: 20
            font.bold: true
            color: "#5C6BC0"
            font.pixelSize: 16
        }
    }

    function saveCurrentTextInputs() {
        let batch = {}
        let textPages = textInputs[currentPage]

        for (let key in textPages) {
            batch[key] = textPages[key]
        }

        sendBatch(batch, currentPage)
//        console.log(maxPages)
        savePageNums(maxPages)
    }

    function sendBatch(batch, key) {
        let url = `http://${ip}:7379/hset/${hash}/${key}/${JSON.stringify(
                batch)}`

        //        console.log("Batch Save URL:", url);
        let xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function () {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status !== 200) {
                    console.error("Error:", xhr.statusText)
                }
            }
        }
        xhr.open("GET", url)
        xhr.send()
    }

    function loadCurrentTextInputs() {
        fetchData(currentPage)
    }

    function fetchData(key) {
        let url = `http://${ip}:7379/hget/${hash}/${key}`
        let xhr = new XMLHttpRequest()

        xhr.onreadystatechange = function () {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status === 200) {
                    let data = JSON.parse(xhr.responseText).hget
                    //                    console.log("Load Data:", data);
                    //                    console.log("Data type:", typeof data);
                    let parsedData = JSON.parse(data)
                    for (let field in parsedData) {
                        if (textInputObjs[key][field]) {
                            textInputObjs[key][field].text = parsedData[field]
                        }
                    }
                } else {
                    console.error("Error:", xhr.statusText)
                }
            }
        }
        xhr.open("GET", url)
        xhr.send()
    }

    function fetchPageNums() {
        let url = `http://${ip}:7379/hget/TS1:Mechanism:RotorLife/nums`
        let xhr = new XMLHttpRequest()

        xhr.onreadystatechange = function () {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status === 200) {
                    let data = JSON.parse(xhr.responseText).hget
                    maxPages = data
//                    console.log(data, maxPages)
                } else {
                    console.error("Error:", xhr.statusText)
                }
            }
        }
        xhr.open("GET", url)
        xhr.send()
    }

    function savePageNums(value) {
        let url = `http://${ip}:7379/hset/TS1:Mechanism:RotorLife/nums/${value}`
        let xhr = new XMLHttpRequest()

        xhr.onreadystatechange = function () {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status !== 200) {
                    console.error("Error:", xhr.statusText)
                }
            }
        }
        xhr.open("GET", url)
        xhr.send()
    }
}
