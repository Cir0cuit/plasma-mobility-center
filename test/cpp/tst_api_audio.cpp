// SPDX-License-Identifier: GPL-2.0-or-later
//
// Freezes the public API of the .audio module, as the QML package uses it.

#include <QTest>

#include "apicheck.h"

using namespace Qt::StringLiterals;

#include "audiobackend.h"
#include "devicefiltermodel.h"

class TestApiAudio : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void audioBackend()
    {
        const QMetaObject &mo = AudioBackend::staticMetaObject;
        MC_REQUIRE_CONVENTIONS(mo);
        MC_REQUIRE_PROPERTIES(mo,
                              QStringList({u"connecting"_s,
                                           u"failed"_s,
                                           u"autoConnecting"_s,
                                           u"defaultSink"_s,
                                           u"defaultSource"_s,
                                           u"outputs"_s,
                                           u"inputs"_s,
                                           u"outputVolumePercent"_s,
                                           u"outputMuted"_s,
                                           u"outputDescription"_s,
                                           u"outputName"_s,
                                           u"outputIconName"_s,
                                           u"inputAvailable"_s,
                                           u"inputVolumePercent"_s,
                                           u"inputMuted"_s,
                                           u"inputDescription"_s,
                                           u"inputName"_s,
                                           u"volumeStep"_s,
                                           u"raiseMaximum"_s,
                                           u"maxVolumePercent"_s,
                                           u"feedbackAvailable"_s,
                                           u"feedbackEnabled rw"_s}));
        MC_REQUIRE_METHODS(mo,
                           "setDefaultOutput(QString)",
                           "setDefaultInput(QString)",
                           "setVolumePercent(QObject*,int)",
                           "setOutputVolumePercent(int)",
                           "setInputVolumePercent(int)",
                           "toggleMute(QObject*)",
                           "toggleOutputMute()",
                           "toggleInputMute()",
                           "volumePercent(QObject*)",
                           "playFeedback()",
                           "reconnect()");
    }

    void deviceFilterModel()
    {
        const QMetaObject &mo = DeviceFilterModel::staticMetaObject;
        MC_REQUIRE_PROPERTIES(mo, QStringList({u"count"_s, u"kind"_s, u"showVirtual rw"_s}));
        MC_REQUIRE_METHODS(mo, "get(int)");

        // Without a source model there are no rows.
        DeviceFilterModel model(DeviceFilterModel::Outputs);
        QCOMPARE(model.count(), 0);
        const auto roles = model.roleNames();
        QVERIFY(roles.values().contains("percent"));
        QVERIFY(roles.values().contains("label"));
    }
};

QTEST_GUILESS_MAIN(TestApiAudio)
#include "tst_api_audio.moc"
