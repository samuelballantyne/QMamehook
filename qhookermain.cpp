#include "qhookermain.h"
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QThread>

// TODO: core app stuff isn't really needed, looks very messy.

qhookerMain::qhookerMain(QObject *parent)
    : QObject{parent}
{
    // get the instance of the main application
    mainApp = QCoreApplication::instance();
}

void qhookerMain::run()
{
    //qDebug() << "Main app is running!";
    //connect(&tcpSocket, &QAbstractSocket::readyRead, this, &qhookerMain::ReadyRead);
    //connect(&tcpSocket, &QAbstractSocket::errorOccurred, this, &qhookerMain::errorOccurred);

    printf("Starting QMamehook v%s\n\n", QMH_VERSION);

    SerialInit();

    printf("\nWaiting for MAME-compatible Network Output @ localhost:8000 ...\n");

    while(true) {
        switch(tcpSocket.state()) {
        case QAbstractSocket::UnconnectedState:
            tcpSocket.connectToHost("localhost", 8000);

            if(tcpSocket.waitForConnected(5000))
                printf("Connected to output server instance!\n");
            else {
                SerialInit();
                QThread::sleep(1);
            }

            break;
        case QAbstractSocket::ConnectedState:
            while(tcpSocket.state() == QAbstractSocket::ConnectedState) {
                // in case of emergency for wendies, set to (+)1 instead
                // possible performance implications here?
                #ifdef Q_OS_WIN
                if(tcpSocket.waitForReadyRead(1)) {
                #else
                if(tcpSocket.waitForReadyRead(-1)) {
                #endif // Q_OS_WIN
                    while(!tcpSocket.atEnd())
                        ReadyRead();

                // Apparently wendies maybe possibly might make false positives here,
                // so check if the error is actually the host being closed, to at least stop it from ending early.
                } else if(tcpSocket.error() == QAbstractSocket::RemoteHostClosedError) {
                    printf("Server closing, disconnecting...\n");
                    tcpSocket.abort();

                    if(!gameName.isEmpty()) {
                        gameName.clear();

                        if(settings && settings->contains("MameStop") && settings->value("MameStop").type() == QMetaType::QStringList) {
                            QStringList tempBuffer = settings->value("MameStop").toStringList();
                            //qInfo() << tempBuffer;
                            while(!tempBuffer.isEmpty()) {
                                if(tempBuffer.at(0).contains("cmw")) {
                                    int portNum = tempBuffer.at(0).at(4).digitValue()-1;
                                    if(portNum >= 0 && portNum < validIDs.size()) {
                                        if(serialPort.at(portNum)->isOpen()) {
                                            serialPort.at(portNum)->write(tempBuffer.at(0).mid(6).toLocal8Bit());
                                            if(!serialPort.at(portNum)->waitForBytesWritten(500))
                                                printf("Wrote to port no. %d (%04X:%04X @ %s), but wasn't sent in time apparently!? 73\n",
                                                       portNum+1, validDevices.at(portNum).vendorIdentifier(), validDevices.at(portNum).productIdentifier(), serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                        } else  printf("Requested to write to port no. %d (%04X:%04X @ %s), but it's not even open yet!\n",
                                                   portNum+1, validDevices.at(portNum).vendorIdentifier(), validDevices.at(portNum).productIdentifier(), serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                    }
                                } else if(tempBuffer.at(0).contains("cmc")) {
                                    // close serial port at number (index(4))
                                    int portNum = tempBuffer.at(0).at(4).digitValue()-1;
                                    if(portNum >= 0 && portNum < validIDs.size()) {
                                        if(serialPort.at(portNum)->isOpen()) {
                                            serialPort.at(portNum)->close();
                                            printf("Closed port no. %d (%04X:%04X @ %s)\n",
                                                   portNum+1, validDevices.at(portNum).vendorIdentifier(), validDevices.at(portNum).productIdentifier(), serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                        } else printf("Waaaaait a second... Port %d (%04X:%04X @ %s) is already closed!\n",
                                                   portNum+1, validDevices.at(portNum).vendorIdentifier(), validDevices.at(portNum).productIdentifier(), serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                    }
                                }
                                tempBuffer.removeFirst();
                            }

                            for(int portNum = 0; portNum < validIDs.size(); ++portNum)
                                if(serialPort.at(portNum)->isOpen()) {
                                    serialPort.at(portNum)->write("E");
                                    serialPort.at(portNum)->waitForBytesWritten(500);
                                    serialPort.at(portNum)->close();
                                    printf("Force-closed port no. %d (%04X:%04X @ %s) - was opened incidentally without a corresponding close command.\n",
                                           portNum+1, validDevices.at(portNum).vendorIdentifier(), validDevices.at(portNum).productIdentifier(), serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                }

                            delete settings;
                            settingsMap.clear();
                        } else for(int portNum = 0; portNum < validIDs.size(); ++portNum) {
                            if(serialPort.at(portNum)->isOpen()) {
                                serialPort.at(portNum)->write("E");
                                if(serialPort.at(portNum)->waitForBytesWritten(500)) {
                                    serialPort.at(portNum)->close();
                                    printf("Force-closed port no. %d (%04X:%04X @ %s) since this game has no MameStop entry.\n",
                                           portNum+1, validDevices.at(portNum).vendorIdentifier(), validDevices.at(portNum).productIdentifier(), serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                } else printf("Sent close signal to port %d (%04X:%04X @ %s), but wasn't sent in time apparently!? 111\n",
                                           portNum+1, validDevices.at(portNum).vendorIdentifier(), validDevices.at(portNum).productIdentifier(), serialPort.at(portNum)->portName().toLocal8Bit().constData());
                            }
                        }
                    }

                    if (closeOnDisconnect) {
                        printf("Application closing due to -c argument.\n");
                        quit();
                        return;
                    }
                }
            }
            break;
        default:
            break;
        }
    }
}


void qhookerMain::quit()
{
    emit finished();
}

// shortly after quit is called the CoreApplication will signal this routine
// this is a good place to delete any objects that were created in the
// constructor and/or to stop any threads
void qhookerMain::aboutToQuitApp()
{
    // stop threads
    // sleep(1);   // wait for threads to stop.
    // delete any objects
}


void qhookerMain::SerialInit()
{
    QList<QSerialPortInfo> serialFoundList = QSerialPortInfo::availablePorts();

    if(serialFoundList.isEmpty()) {
        validDevices.clear();
        validIDs.clear();
        if(serialPort.count()) {
            for(auto &port : serialPort)
                delete port;
            serialPort.clear();
            printf("No devices found!\n");
        }
    } else {
        QList<QSerialPortInfo> newDevices;
        // Filter devices based on Vendor IDs:
        for (const QSerialPortInfo &info : std::as_const(serialFoundList)) {
            if(info.vendorIdentifier() == 9025   || // JB
               info.vendorIdentifier() == 13939  || // Props3D
               info.vendorIdentifier() == 0xF143 || // OpenFIRE
               (info.vendorIdentifier() == 0x0483 && info.productIdentifier() == 0x5740)) // RS3 Reaper (requires manual modprobe by user)
                newDevices.append(info);
            else if(!info.portName().startsWith("ttyS"))
                printf("Unknown device found: %s\n", info.portName().toLocal8Bit().constData());
        }

        if(newDevices.isEmpty()) {
            validDevices.clear();
            validIDs.clear();
            if(serialPort.count()) {
                for(auto &port : serialPort)
                    delete port;
                serialPort.clear();
                printf("No devices found!\n");
            }
        } else {
            if(newDevices.size() != validDevices.size()) {
                printf("Current ports list does not match new list, overriding...\n\n");
                AddNewDevices(newDevices);
            } else for(const auto &newPort : std::as_const(newDevices)) {
                if(!validIDs.contains(newPort.vendorIdentifier() | newPort.productIdentifier() << 16)) {
                    printf("%04X:%04X not found in current ports, overriding old serial devices list...\n\n",
                           newPort.vendorIdentifier(), newPort.productIdentifier());
                    AddNewDevices(newDevices);
                    break;
                }
            }
        }
    }
}


void qhookerMain::AddNewDevices(QList<QSerialPortInfo> &newDevices)
{
    if(serialPort.count()) {
        for(auto &port : serialPort)
            delete port;
        serialPort.clear();
    }

    // Create our array of QSerialPorts, sized to the number of valid devices
    for(const auto &device : newDevices)
        serialPort << new QSerialPort;

    switch(sortType) {
    case sortPIDascend:
        std::sort(newDevices.begin(), newDevices.end(),
                  [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
                      return a.productIdentifier() < b.productIdentifier();
                  });
        break;
    case sortPIDdescend:
        std::sort(newDevices.begin(), newDevices.end(),
                  [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
                      return a.productIdentifier() > b.productIdentifier();
                  });
        break;
    case sortPortAscend:
        std::sort(newDevices.begin(), newDevices.end(),
                  [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
                      return a.portName().mid(
                                             #ifdef Q_OS_WIN
                                             3 // COM
                                             #else
                                             6 // ttyACM
                                             #endif
                                             ).toInt() < b.portName().mid(
                                                                          #ifdef Q_OS_WIN
                                                                          3
                                                                          #else
                                                                          6
                                                                          #endif
                                                                          ).toInt();
                  });
        break;
    case sortPortDescend:
        std::sort(newDevices.begin(), newDevices.end(),
                  [](const QSerialPortInfo &a, const QSerialPortInfo &b) {
                      return a.portName().mid(
                                             #ifdef Q_OS_WIN
                                             3 // COM
                                             #else
                                             6 // ttyACM
                                             #endif
                                             ).toInt() > b.portName().mid(
                                                                         #ifdef Q_OS_WIN
                                                                         3
                                                                         #else
                                                                         6
                                                                         #endif
                                                                         ).toInt();
                  });
        break;
    default:
        break;
    }

    for (const QSerialPortInfo &info : newDevices) {
        printf("========================================\n");
        printf("Port Name: %s\n", info.portName().toLocal8Bit().constData());
        printf("Vendor Identifier:  ");
        if(info.hasVendorIdentifier()) {
            printf("%04X ", info.vendorIdentifier());
            switch(info.vendorIdentifier()) {
            case 9025:
                printf("(GUN4IR Lightgun)\n");
                break;
            case 13939:
                printf("(Blamcon Lightgun)\n");
                break;
            case 0x0483:
                printf("(RetroShooter Lightgun)");
                break;
            case 0xF143:
                printf("(OpenFIRE Lightgun)\n");
                break;
            default:
                // unlikely to happen due to whitelisting, but just in case.
                printf("\n");
                break;
            }
        } else printf("N/A\n");

        printf("Product Identifier: ");
        if(info.hasProductIdentifier())
            printf("%04X\n", info.productIdentifier());
        else printf("N/A\n");

        if(!info.manufacturer().isEmpty() && !info.description().isEmpty())
            printf("Device: %s %s\n", info.manufacturer().toLocal8Bit().constData(), info.description().toLocal8Bit().constData());
        printf("========================================\n");
    }

    printf("\n");

    // Keep track of assigned PIDs and check for duplicates
    QSet<uint32_t> newPids;
    bool duplicateProductIds = false;
    int i = 0;

    // Assign indices (ports) in sorted order (lowest PID → highest PID)
    for(const auto &device : std::as_const(newDevices)) {
        // Check for duplicates
        if(newPids.contains(device.productIdentifier())) {
            duplicateProductIds = true;
            printf("Duplicate Device %04X:%04X found on device %s\n",
                   device.vendorIdentifier(), device.productIdentifier(), device.portName().toLocal8Bit().constData());
        } else {
            newPids.insert(device.vendorIdentifier() | device.productIdentifier() << 16);
        }

        // Assigning device to serialPort array
        serialPort.at(i)->setPort(device);
        serialPort.at(i)->setBaudRate(QSerialPort::Baud9600);
        serialPort.at(i)->setDataBits(QSerialPort::Data8);
        serialPort.at(i)->setParity(QSerialPort::NoParity);
        serialPort.at(i)->setStopBits(QSerialPort::OneStop);
        serialPort.at(i++)->setFlowControl(QSerialPort::NoFlowControl);

        printf("Assigning %s (%04X:%04X) to port no. %d\n",
               device.portName().toLocal8Bit().constData(), device.vendorIdentifier(), device.productIdentifier(), i);
    }

    validDevices = newDevices;
    validIDs = newPids;

    if(duplicateProductIds)
        printf("Matching identifiers detected.\n"
               "To get consistent port allocations, each gun should have differentiating Product IDs.\n");
}


bool qhookerMain::GameSearching(const QString &input)
{
    if(buffer.isEmpty()) {
        // Split the output in case of connecting mid-way.
        buffer = input.split('\r', Qt::SkipEmptyParts);
    }
    //qDebug() << buffer;
    while(!buffer.isEmpty()) {
        buffer[0] = buffer.at(0).trimmed();

        // flycast outputs its start signal with code "game" using a game's full title instead of a mame zip name
        if(buffer.at(0).startsWith("mame_start =") || buffer.at(0).startsWith("game =")) {
            // flycast (standalone) ALSO doesn't disconnect at any point,
            // so we terminate and unload any existing settings if a new gameStart is found while a game is already loaded.
            if(!gameName.isEmpty()) {
                gameName.clear();
                if(settings) {
                    delete settings;
                    settingsMap.clear();
                }
            }

            gameName = buffer[0].mid(input.indexOf('=')+2).trimmed().toLocal8Bit();
            printf("Detected game name: %s\n", gameName.constData());

            if(gameName != "___empty") {
                if(customPathSet) {
                    LoadConfig(customPath + gameName + ".ini");
                } else {
                // TODO: there might be a better path for this? Trying to prevent "../QMamehook/QMamehook/ini" on Windows here.
                LoadConfig(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) +
                #ifndef Q_OS_WIN
                "/QMamehook"
                #endif
                "/ini/" + gameName + ".ini");
                }

if (settings->contains("MameStart")) {
    //qInfo() << "Detected start statement:";
    QStringList tempBuffer = settings->value("MameStart").toStringList();
    //qInfo() << tempBuffer;

    while (!tempBuffer.isEmpty()) {
        QString cmd = tempBuffer.at(0).trimmed();

       if (cmd.contains("cmo")) {
    // open serial port at number (index 4)
    int portNum = cmd.at(4).digitValue() - 1;
    if (portNum >= 0 && portNum < validIDs.size()) {
        QSerialPort *port = serialPort.at(portNum);
        if (!port) {
            printf("MameStart: serialPort[%d] is null!\n", portNum);
        } else if (!port->isOpen()) {
            // Try to open
            if (!port->open(QIODevice::WriteOnly)) {
                printf("MameStart: FAILED to open port no. %d (%04X:%04X @ %s): %s\n",
                       portNum + 1,
                       validDevices.at(portNum).vendorIdentifier(),
                       validDevices.at(portNum).productIdentifier(),
                       port->portName().toLocal8Bit().constData(),
                       port->errorString().toLocal8Bit().constData());
            } else {
                // Only set DTR if open() actually worked
                port->setDataTerminalReady(true);
                printf("Opened port no. %d (%04X:%04X @ %s)\n",
                       portNum + 1,
                       validDevices.at(portNum).vendorIdentifier(),
                       validDevices.at(portNum).productIdentifier(),
                       port->portName().toLocal8Bit().constData());
            }
        } else {
            printf("Waaaaait a second... Port %d (%04X:%04X @ %s) is already open!\n",
                   portNum + 1,
                   validDevices.at(portNum).vendorIdentifier(),
                   validDevices.at(portNum).productIdentifier(),
                   port->portName().toLocal8Bit().constData());
        }
    }
} else if (cmd.contains("cmw")) {
            // *** This is where S and other startup commands are sent ***
            int portNum = cmd.at(4).digitValue() - 1;
            if (portNum >= 0 && portNum < validIDs.size()) {
                if (serialPort.at(portNum)->isOpen()) {
                    QByteArray payload = cmd.mid(6).toLocal8Bit();

                    // Debug so we can *see* S going out:
                    printf("MameStart: writing to port %d: '%s'\n",
                           portNum + 1,
                           payload.constData());

                    serialPort.at(portNum)->write(payload);

                    // For this one-off startup path, it’s OK to block briefly.
                    if (!serialPort.at(portNum)->waitForBytesWritten(500)) {
                        printf("MameStart: wrote to port no. %d (%04X:%04X @ %s), "
                               "but wasn't sent in time apparently! (startup)\n",
                               portNum + 1,
                               validDevices.at(portNum).vendorIdentifier(),
                               validDevices.at(portNum).productIdentifier(),
                               serialPort.at(portNum)->portName().toLocal8Bit().constData());
                    }
                } else {
                    printf("MameStart: requested to write to port no. %d (%04X:%04X @ %s), "
                           "but it's not even open yet!\n",
                           portNum + 1,
                           validDevices.at(portNum).vendorIdentifier(),
                           validDevices.at(portNum).productIdentifier(),
                           serialPort.at(portNum)->portName().toLocal8Bit().constData());
                }
            }
        }

        tempBuffer.removeFirst();
    }
}

                buffer.removeFirst();
                return true;
            } else {
                gameName.clear();
            }
        }
        buffer.removeFirst();
    }
    return false;
}


bool qhookerMain::GameStarted(const QString &input)
{
    // If we were called with fresh input, prime the buffer
    if (buffer.isEmpty())
        buffer = input.split('\r', Qt::SkipEmptyParts);

    while (!buffer.isEmpty()) {
        buffer[0] = buffer.at(0).trimmed();
        if (buffer[0].isEmpty()) {
            buffer.removeFirst();
            continue;
        }

        if (verbosity)
            printf("%s\n", buffer[0].toLocal8Bit().constData());

        const QString func = buffer[0].left(buffer[0].indexOf(' '));

        // ---------------------------------------------------------------------
        // mame_stop: clean up and close ports
        // ---------------------------------------------------------------------
        if (func == "mame_stop") {
            printf("mame_stop signal received, disconnecting...\n");

            if (!gameName.isEmpty()) {
                gameName.clear();

                if (settings && settings->contains("MameStop") &&
                    settings->value("MameStop").type() == QMetaType::QStringList) {

                    QStringList tempBuffer = settings->value("MameStop").toStringList();

                    while (!tempBuffer.isEmpty()) {
                        const QString &cmd = tempBuffer.at(0);

                        if (cmd.contains("cmw")) {
                            int portNum = cmd.at(4).digitValue() - 1;

                            if (portNum >= 0 && portNum < validIDs.size()) {
                                if (serialPort.at(portNum)->isOpen()) {
                                    QByteArray payload = cmd.mid(6).toLocal8Bit();
                                    serialPort.at(portNum)->write(payload);
                                    // we can afford to block a bit on shutdown
                                    if (!serialPort.at(portNum)->waitForBytesWritten(500)) {
                                        printf("Wrote to port no. %d (%04X:%04X @ %s), "
                                               "but wasn't sent in time apparently!? (MameStop)\n",
                                               portNum + 1,
                                               validDevices.at(portNum).vendorIdentifier(),
                                               validDevices.at(portNum).productIdentifier(),
                                               serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                    }
                                } else {
                                    printf("Requested to write to port no. %d (%04X:%04X @ %s), "
                                           "but it's not even open yet!\n",
                                           portNum + 1,
                                           validDevices.at(portNum).vendorIdentifier(),
                                           validDevices.at(portNum).productIdentifier(),
                                           serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                }
                            }
                        } else if (cmd.contains("cmc")) {
                            int portNum = cmd.at(4).digitValue() - 1;

                            if (portNum >= 0 && portNum < validIDs.size()) {
                                if (serialPort.at(portNum)->isOpen()) {
                                    serialPort.at(portNum)->close();
                                    printf("Closed port no. %d (%04X:%04X @ %s)\n",
                                           portNum + 1,
                                           validDevices.at(portNum).vendorIdentifier(),
                                           validDevices.at(portNum).productIdentifier(),
                                           serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                } else {
                                    printf("Waaaaait a second... Port %d (%04X:%04X @ %s) is already closed!\n",
                                           portNum + 1,
                                           validDevices.at(portNum).vendorIdentifier(),
                                           validDevices.at(portNum).productIdentifier(),
                                           serialPort.at(portNum)->portName().toLocal8Bit().constData());
                                }
                            }
                        }

                        tempBuffer.removeFirst();
                    }

                    // Force close any stragglers that were opened but never closed
                    for (int portNum = 0; portNum < validIDs.size(); ++portNum) {
                        if (serialPort.at(portNum)->isOpen()) {
                            serialPort.at(portNum)->write("E");
                            serialPort.at(portNum)->waitForBytesWritten(500);
                            serialPort.at(portNum)->close();
                            printf("Force-closed port no. %d (%04X:%04X @ %s) - was opened incidentally "
                                   "without a corresponding close command.\n",
                                   portNum + 1,
                                   validDevices.at(portNum).vendorIdentifier(),
                                   validDevices.at(portNum).productIdentifier(),
                                   serialPort.at(portNum)->portName().toLocal8Bit().constData());
                        }
                    }

                    delete settings;
                    settingsMap.clear();
                } else {
                    // No explicit MameStop: just blast "E" and close
                    for (int portNum = 0; portNum < validIDs.size(); ++portNum) {
                        if (serialPort.at(portNum)->isOpen()) {
                            serialPort.at(portNum)->write("E");
                            if (serialPort.at(portNum)->waitForBytesWritten(500)) {
                                serialPort.at(portNum)->close();
                                printf("Force-closed port no. %d (%04X:%04X @ %s) since this game has no MameStop entry.\n",
                                       portNum + 1,
                                       validDevices.at(portNum).vendorIdentifier(),
                                       validDevices.at(portNum).productIdentifier(),
                                       serialPort.at(portNum)->portName().toLocal8Bit().constData());
                            } else {
                                printf("Sent close signal to port %d (%04X:%04X @ %s), "
                                       "but wasn't sent in time apparently!?\n",
                                       portNum + 1,
                                       validDevices.at(portNum).vendorIdentifier(),
                                       validDevices.at(portNum).productIdentifier(),
                                       serialPort.at(portNum)->portName().toLocal8Bit().constData());
                            }
                        }
                    }
                }
            }

            buffer.clear();
            return true;

        // ---------------------------------------------------------------------
        // Live game outputs → serial commands
        // ---------------------------------------------------------------------
        } else if (!settingsMap[func].isEmpty()) {

            const QString mapping = settingsMap.value(func);

            // Branch 1: on/off style mapping (left|right)
            if (mapping.contains('|')) {
                // MAME network output always ends in " = 0" or " = 1" for these
                const bool stateIsOne = buffer[0].right(1).toInt() != 0;

                const int pipeIndex = mapping.indexOf('|');
                const QString sideStr = stateIsOne
                    ? mapping.mid(pipeIndex + 1)
                    : mapping.left(pipeIndex);

                QStringList actions = sideStr.split(',', Qt::SkipEmptyParts);

                for (QString action : actions) {
                    if (!action.contains("cmw"))
                        continue;

                    const int cmwIndex = action.indexOf("cmw");
                    if (cmwIndex < 0 || cmwIndex + 5 >= action.size())
                        continue;

                    const int portNum = action.at(cmwIndex + 4).digitValue() - 1;
                    if (portNum < 0 || portNum >= validIDs.size())
                        continue;

                    QSerialPort *port = serialPort.at(portNum);
                    if (!port || !port->isOpen()) {
                        printf("Requested to write to port no. %d (%04X:%04X @ %s), "
                               "but it's not open! (GameStarted)\n",
                               portNum + 1,
                               validDevices.at(portNum).vendorIdentifier(),
                               validDevices.at(portNum).productIdentifier(),
                               port ? port->portName().toLocal8Bit().constData() : "<null>");
                        continue;
                    }

                    // In case someone used %s% on a boolean output…
                    if (action.contains("%s%"))
                        action.replace("%s%", stateIsOne ? "1" : "0");

                    QByteArray payload =
                        action.mid(cmwIndex + 6).toLocal8Bit();

                    if (payload.isEmpty())
                        continue;

                    port->write(payload);
                    // Non-blocking; let OS flush this
                    // port->flush(); // optional – can test if needed

                    if (verbosity > 1) {
                        printf("GameStarted[%s, %s]: wrote to port %d: '%s'\n",
                               func.toLocal8Bit().constData(),
                               stateIsOne ? "right/1" : "left/0",
                               portNum + 1,
                               payload.constData());
                    }
                }

            // Branch 2: scalar (%s%) style mapping
            } else {
                int stateVal = 0;
                const int eqIndex = buffer[0].indexOf('=');
                if (eqIndex >= 0)
                    stateVal = buffer[0].mid(eqIndex + 1).trimmed().toInt();

                QStringList actions = mapping.split(',', Qt::SkipEmptyParts);

                for (QString action : actions) {
                    if (!action.contains("cmw"))
                        continue;

                    const int cmwIndex = action.indexOf("cmw");
                    if (cmwIndex < 0 || cmwIndex + 5 >= action.size())
                        continue;

                    const int portNum = action.at(cmwIndex + 4).digitValue() - 1;
                    if (portNum < 0 || portNum >= validIDs.size())
                        continue;

                    QSerialPort *port = serialPort.at(portNum);
                    if (!port || !port->isOpen()) {
                        printf("Requested to write to port no. %d (%04X:%04X @ %s), "
                               "but it's not open! (GameStarted)\n",
                               portNum + 1,
                               validDevices.at(portNum).vendorIdentifier(),
                               validDevices.at(portNum).productIdentifier(),
                               port ? port->portName().toLocal8Bit().constData() : "<null>");
                        continue;
                    }

                    if (action.contains("%s%"))
                        action.replace("%s%", QString::number(stateVal));

                    QByteArray payload =
                        action.mid(cmwIndex + 6).toLocal8Bit();

                    if (payload.isEmpty())
                        continue;

                    port->write(payload);

                    if (verbosity > 1) {
                        printf("GameStarted[%s, val=%d]: wrote to port %d: '%s'\n",
                               func.toLocal8Bit().constData(),
                               stateVal,
                               portNum + 1,
                               payload.constData());
                    }
                }
            }

        // ---------------------------------------------------------------------
        // First time we see this func: create a blank entry in the config
        // ---------------------------------------------------------------------
        } else if (!settings->contains(func)) {
            settings->beginGroup("Output");
            settings->setValue(func, "");
            settingsMap[func] = "";
            settings->endGroup();
        }

        // Consume this line and move on
        buffer.removeFirst();
    }

    return false;
}


void qhookerMain::ReadyRead()
{
    buffer.clear();
    if(gameName.isEmpty()) {
        // if this returns early as true, then zip straight into the GameStarted function with the remaining buffer.
        if(GameSearching(tcpSocket.readLine()))
            GameStarted();
    } else {
        if(GameStarted(tcpSocket.readLine()))
            GameSearching();
    }
}


void qhookerMain::LoadConfig(const QString &path)
{
    settings = new QSettings(path, QSettings::IniFormat);

    if(!settings->contains("MameStart")) {
        printf("Error loading file at: %s\n", path.toLocal8Bit().constData());
        if(!QFile::exists(path) && !path.contains("__empty")) {
            settings->setValue("MameStart", "");
            settings->setValue("MameStop", "");
            settings->setValue("StateChange", "");
            settings->setValue("OnRotate", "");
            settings->setValue("OnPause", "");
            settings->setValue("KeyStates/RefreshTime", "");
        }
    } else printf("Loading: %s\n", path.toLocal8Bit().constData());

    settings->beginGroup("Output");

    QStringList settingsTemp = settings->childKeys();
    for(int i = 0; i < settingsTemp.length(); ++i) {
        // QSettings splits anything with a comma, so we have to stitch the Q-splitted value back together.
        if(settings->value(settingsTemp[i]).type() == QMetaType::QStringList)
             settingsMap[settingsTemp[i]] = settings->value(settingsTemp[i]).toStringList().join(",");
        else settingsMap[settingsTemp[i]] = settings->value(settingsTemp[i]).toString();
    }
    settings->endGroup();
}
