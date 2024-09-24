import QtQuick
import QtQuick.Controls

Item {
    visible: true
    anchors.fill: parent

    property string ip: "localhost"
    property string hash: "TS1:Mechanism:RotorLife"
    property string key: "users"
    property var correctUsers: []
    signal loginSuccessful

    Rectangle {
        anchors.fill: parent
        color: "#2E2E2E"

        Column {
            spacing: 20
            anchors.centerIn: parent
            width: 200

            Text {
                text: "Login"
                color: "#5C6BC0"
                font.pixelSize: 24
                anchors.horizontalCenter: parent.horizontalCenter
            }

            TextField {
                id: usernameField
                placeholderText: "Username"
                width: parent.width
                height: 40
                font.pixelSize: 20
                background: Rectangle {
                    color: "#4A4A4A"
                    radius: 5
                }
            }

            TextField {
                id: passwordField
                placeholderText: "Password"
                echoMode: TextInput.Password
                width: parent.width
                height: 40
                font.pixelSize: 20
                background: Rectangle {
                    color: "#4A4A4A"
                    radius: 5
                }
            }

            Button {
                text: "Login"
                width: parent.width
                height: 40
                font.pixelSize: 18
                background: Rectangle {
                    color: "#5C6BC0"
                    radius: 5
                }
                onClicked: {
                    const hashedPassword = HashProvider.hashPassword(
                                             passwordField.text)
                    for (let user of correctUsers) {
                        if (usernameField.text === user.username
                                && hashedPassword === user.password) {
                            loginSuccessful()
                            return
                        }
                    }
                    loginMessage.text = "Incorrect username or password."
                }
            }

            Text {
                id: loginMessage
                color: "red"
                font.pixelSize: 16
                anchors.horizontalCenter: parent.horizontalCenter
            }
        }
    }

    Component.onCompleted: {
        fetchUsers()
    }

    function fetchUsers() {
        let url = `http://${ip}:7379/hget/${hash}/${key}`
        let xhr = new XMLHttpRequest()

        xhr.onreadystatechange = function () {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                if (xhr.status === 200) {
                    let data = JSON.parse(xhr.responseText).hget
//                    console.log(data);
                    let parsedData = JSON.parse(data)
                    for (let username in parsedData) {
                        correctUsers.push({
                                              "username": username,
                                              "password": parsedData[username]
                                          })
                    }
                } else {
                    console.error("Error:", xhr.statusText)
                }
            }
        }
        xhr.open("GET", url)
        xhr.send()
    }
}
