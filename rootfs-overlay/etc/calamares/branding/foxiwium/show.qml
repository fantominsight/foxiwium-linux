/*
 * Foxiwium Linux — Calamares slideshow
 * Shows a rotating set of informational slides during installation.
 */

import QtQuick 2.0;
import calamares.slideshow 1.0;

Presentation
{
    id: presentation

    Timer {
        interval: 15000
        repeat: true
        onTriggered: presentation.goToNextSlide()
    }

    Slide {
        anchors.fill: parent
        Image {
            id: slide1Img
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width * 0.9
            height: parent.height * 0.55
            source: "welcome.png"
            fillMode: Image.PreserveAspectFit
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: slide1Img.bottom
            anchors.topMargin: 10
            width: parent.width * 0.9
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#e8c890"
            font.pointSize: 12
            text: qsTr("Foxiwium Linux 1.0 (Fenrir) — Debian-based live system\nSimple. Fast. Fox-like.")
        }
    }

    Slide {
        anchors.fill: parent
        Text {
            anchors.centerIn: parent
            width: parent.width * 0.8
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#e8c890"
            font.pointSize: 12
            text: qsTr("Install Foxiwium Linux\n\n- KDE Plasma on X11\n- NetworkManager (wired + wifi)\n- Works on old NVIDIA GPUs (GT 210 & friends) via nouveau\n- GRUB bootloader, BIOS and UEFI\n\nThank you for choosing Foxiwium!")
        }
    }
}
