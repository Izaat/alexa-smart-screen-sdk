/*
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *     http://aws.amazon.com/apache2.0/
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#include <ADSL/MessageInterpreter.h>
#include <Audio/SystemSoundAudioFactory.h>
#include <AVSCommon/AVS/Attachment/AttachmentManager.h>
#include <AVSCommon/AVS/ExceptionEncounteredSender.h>
#include <AVSCommon/SDKInterfaces/InternetConnectionMonitorInterface.h>
#include <AVSCommon/Utils/Bluetooth/BluetoothEventBus.h>
#include <AVSCommon/Utils/Network/InternetConnectionMonitor.h>
#include <AVSCommon/Utils/Metrics/MetricRecorderInterface.h>
#include <AVSCommon/Utils/Metrics/MetricEventBuilder.h>

#ifdef ACSDK_ENABLE_METRICS_RECORDING
#include <Metrics/MetricRecorder.h>
#endif

#include <SystemSoundPlayer/SystemSoundPlayer.h>

#ifdef ENABLE_CAPTIONS
#include <Captions/LibwebvttParserAdapter.h>
#include <Captions/TimingAdapterFactory.h>
#endif

#ifdef ENABLE_OPUS
#include <SpeechEncoder/OpusEncoderContext.h>
#endif

#ifdef ENABLE_COMMS
#include <CallManager/CallManager.h>
#include <CallManager/SipUserAgent.h>
#endif

#ifdef ENABLE_COMMS_AUDIO_PROXY
#include <CallManager/CallAudioDeviceProxy.h>
#endif

#ifdef ENABLE_PCC
#include <AVSCommon/SDKInterfaces/Phone/PhoneCallerInterface.h>
#include <PhoneCallController/PhoneCallController.h>
#endif

#ifdef ENABLE_MCC
#include <AVSCommon/SDKInterfaces/Calendar/CalendarClientInterface.h>
#include <AVSCommon/SDKInterfaces/Meeting/MeetingClientInterface.h>
#include <MeetingClientController/MeetingClientController.h>
#endif

#if defined(ENABLE_MRM) && defined(ENABLE_MRM_STANDALONE_APP)
#include <MRMHandler/MRMHandlerProxy.h>
#elif ENABLE_MRM
#include <MRMHandler/MRMHandler.h>
#endif

#include <AVSCommon/AVS/SpeakerConstants/SpeakerConstants.h>
#include <InterruptModel/InterruptModel.h>
#include <System/LocaleHandler.h>
#include <System/ReportStateHandler.h>
#include <System/SystemCapabilityProvider.h>
#include <System/TimeZoneHandler.h>
#include <System/UserInactivityMonitor.h>

#ifdef BLUETOOTH_BLUEZ
#include <BlueZ/BlueZBluetoothDeviceManager.h>
#include <Bluetooth/BluetoothAVRCPTransformer.h>
#endif

#include "SmartScreenClient/SmartScreenClient.h"
#include "SmartScreenClient/DeviceSettingsManagerBuilder.h"

namespace alexaSmartScreenSDK {
namespace smartScreenClient {

using namespace alexaClientSDK;
using namespace alexaClientSDK::avsCommon::avs;
using namespace alexaClientSDK::avsCommon::sdkInterfaces;
using namespace alexaClientSDK::avsCommon::sdkInterfaces::endpoints;
using namespace alexaClientSDK::endpoints;

/// Key for audio channel array configurations in configuration node.
static const std::string AUDIO_CHANNEL_CONFIG_KEY = "audioChannels";

/// Key for the interrupt model configuration
static const std::string INTERRUPT_MODEL_CONFIG_KEY = "interruptModel";

/// String to identify log entries originating from this file.
static const std::string TAG("SmartScreenClient");

/**
 * Create a LogEntry using this file's TAG and the specified event string.
 *
 * @param The event string for this @c LogEntry.
 */
#define LX(event) alexaClientSDK::avsCommon::utils::logger::LogEntry(TAG, event)

std::unique_ptr<SmartScreenClient> SmartScreenClient::create(
    std::shared_ptr<avsCommon::utils::DeviceInfo> deviceInfo,
    std::shared_ptr<registrationManager::CustomerDataManager> customerDataManager,
    const std::unordered_map<std::string, std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface>>&
        externalMusicProviderMediaPlayers,
    const std::unordered_map<std::string, std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface>>&
        externalMusicProviderSpeakers,
    const capabilityAgents::externalMediaPlayer::ExternalMediaPlayer::AdapterCreationMap& adapterCreationMap,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> speakMediaPlayer,
    std::unique_ptr<avsCommon::utils::mediaPlayer::MediaPlayerFactoryInterface> audioMediaPlayerFactory,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> alertsMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> notificationsMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> bluetoothMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> ringtoneMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> systemSoundMediaPlayer,
    std::unique_ptr<avsCommon::utils::metrics::MetricSinkInterface> metricSinkInterface,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> speakSpeaker,
    std::vector<std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::SpeakerInterface>> audioSpeakers,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> alertsSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> notificationsSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> bluetoothSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> ringtoneSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> systemSoundSpeaker,
    const std::multimap<
        alexaClientSDK::avsCommon::sdkInterfaces::ChannelVolumeInterface::Type,
        std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::SpeakerInterface>> additionalSpeakers,
#ifdef ENABLE_PCC
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> phoneSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::phone::PhoneCallerInterface> phoneCaller,
#endif
#ifdef ENABLE_MCC
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> meetingSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::meeting::MeetingClientInterface> meetingClient,
    std::shared_ptr<avsCommon::sdkInterfaces::calendar::CalendarClientInterface> calendarClient,
#endif
#ifdef ENABLE_COMMS_AUDIO_PROXY
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> commsMediaPlayer,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> commsSpeaker,
    std::shared_ptr<alexaClientSDK::avsCommon::avs::AudioInputStream> sharedDataStream,
#endif
    std::shared_ptr<EqualizerRuntimeSetup> equalizerRuntimeSetup,
    std::shared_ptr<avsCommon::sdkInterfaces::audio::AudioFactoryInterface> audioFactory,
    std::shared_ptr<avsCommon::sdkInterfaces::AuthDelegateInterface> authDelegate,
    std::shared_ptr<alexaClientSDK::acsdkAlerts::storage::AlertStorageInterface> alertStorage,
    std::shared_ptr<certifiedSender::MessageStorageInterface> messageStorage,
    std::shared_ptr<alexaClientSDK::acsdkNotificationsInterfaces::NotificationsStorageInterface> notificationsStorage,
    std::unique_ptr<settings::storage::DeviceSettingStorageInterface> deviceSettingStorage,
    std::shared_ptr<alexaClientSDK::acsdkBluetooth::BluetoothStorageInterface> bluetoothStorage,
    std::shared_ptr<avsCommon::sdkInterfaces::storage::MiscStorageInterface> miscStorage,
    std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::DialogUXStateObserverInterface>>
        alexaDialogStateObservers,
    std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::ConnectionStatusObserverInterface>>
        connectionObservers,
    std::shared_ptr<avsCommon::sdkInterfaces::InternetConnectionMonitorInterface> internetConnectionMonitor,
    std::shared_ptr<avsCommon::sdkInterfaces::CapabilitiesDelegateInterface> capabilitiesDelegate,
    std::shared_ptr<avsCommon::sdkInterfaces::ContextManagerInterface> contextManager,
    std::shared_ptr<alexaClientSDK::acl::TransportFactoryInterface> transportFactory,
    std::shared_ptr<avsCommon::sdkInterfaces::LocaleAssetsManagerInterface> localeAssetsManager,
    std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::bluetooth::BluetoothDeviceConnectionRuleInterface>>
        enabledConnectionRules,
    std::shared_ptr<avsCommon::sdkInterfaces::SystemTimeZoneInterface> systemTimezone,
    avsCommon::sdkInterfaces::softwareInfo::FirmwareVersion firmwareVersion,
    bool sendSoftwareInfoOnConnected,
    std::shared_ptr<avsCommon::sdkInterfaces::SoftwareInfoSenderObserverInterface> softwareInfoSenderObserver,
    std::unique_ptr<avsCommon::sdkInterfaces::bluetooth::BluetoothDeviceManagerInterface> bluetoothDeviceManager,
    std::shared_ptr<avsCommon::sdkInterfaces::AVSGatewayManagerInterface> avsGatewayManager,
    std::shared_ptr<avsCommon::sdkInterfaces::PowerResourceManagerInterface> powerResourceManager,
    std::shared_ptr<avsCommon::sdkInterfaces::diagnostics::DiagnosticsInterface> diagnostics,
    const std::shared_ptr<ExternalCapabilitiesBuilderInterface>& externalCapabilitiesBuilder,
    std::shared_ptr<avsCommon::sdkInterfaces::ChannelVolumeFactoryInterface> channelVolumeFactory,
    std::shared_ptr<alexaSmartScreenSDK::smartScreenSDKInterfaces::VisualStateProviderInterface> visualStateProvider,
    const std::string& APLMaxVersion) {
    if (!deviceInfo) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullDeviceInfo"));
        return nullptr;
    }

    std::unique_ptr<SmartScreenClient> smartScreenClient(new SmartScreenClient(*deviceInfo));
    if (!smartScreenClient->initialize(
            customerDataManager,
            externalMusicProviderMediaPlayers,
            externalMusicProviderSpeakers,
            adapterCreationMap,
            speakMediaPlayer,
            std::move(audioMediaPlayerFactory),
            alertsMediaPlayer,
            notificationsMediaPlayer,
            bluetoothMediaPlayer,
            ringtoneMediaPlayer,
            systemSoundMediaPlayer,
            std::move(metricSinkInterface),
            speakSpeaker,
            audioSpeakers,
            alertsSpeaker,
            notificationsSpeaker,
            bluetoothSpeaker,
            ringtoneSpeaker,
            systemSoundSpeaker,
            additionalSpeakers,
#ifdef ENABLE_PCC
            phoneSpeaker,
            phoneCaller,
#endif
#ifdef ENABLE_MCC
            meetingSpeaker,
            meetingClient,
            calendarClient,
#endif
#ifdef ENABLE_COMMS_AUDIO_PROXY
            commsMediaPlayer,
            commsSpeaker,
            sharedDataStream,
#endif
            equalizerRuntimeSetup,
            audioFactory,
            authDelegate,
            alertStorage,
            messageStorage,
            notificationsStorage,
            std::move(deviceSettingStorage),
            bluetoothStorage,
            miscStorage,
            alexaDialogStateObservers,
            connectionObservers,
            internetConnectionMonitor,
            capabilitiesDelegate,
            contextManager,
            transportFactory,
            localeAssetsManager,
            enabledConnectionRules,
            systemTimezone,
            firmwareVersion,
            sendSoftwareInfoOnConnected,
            softwareInfoSenderObserver,
            std::move(bluetoothDeviceManager),
            avsGatewayManager,
            powerResourceManager,
            diagnostics,
            externalCapabilitiesBuilder,
            channelVolumeFactory,
            visualStateProvider,
            APLMaxVersion)) {
        return nullptr;
    }

    return smartScreenClient;
}

SmartScreenClient::SmartScreenClient(const avsCommon::utils::DeviceInfo& deviceInfo) : m_deviceInfo{deviceInfo} {
}

bool SmartScreenClient::initialize(
    std::shared_ptr<registrationManager::CustomerDataManager> customerDataManager,
    const std::unordered_map<std::string, std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface>>&
        externalMusicProviderMediaPlayers,
    const std::unordered_map<std::string, std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface>>&
        externalMusicProviderSpeakers,
    const capabilityAgents::externalMediaPlayer::ExternalMediaPlayer::AdapterCreationMap& adapterCreationMap,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> speakMediaPlayer,
    std::unique_ptr<avsCommon::utils::mediaPlayer::MediaPlayerFactoryInterface> audioMediaPlayerFactory,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> alertsMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> notificationsMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> bluetoothMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> ringtoneMediaPlayer,
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> systemSoundMediaPlayer,
    std::unique_ptr<avsCommon::utils::metrics::MetricSinkInterface> metricSinkInterface,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> speakSpeaker,
    std::vector<std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::SpeakerInterface>> audioSpeakers,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> alertsSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> notificationsSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> bluetoothSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> ringtoneSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> systemSoundSpeaker,
    const std::multimap<
        alexaClientSDK::avsCommon::sdkInterfaces::ChannelVolumeInterface::Type,
        std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::SpeakerInterface>> additionalSpeakers,
#ifdef ENABLE_PCC
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> phoneSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::phone::PhoneCallerInterface> phoneCaller,
#endif
#ifdef ENABLE_MCC
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> meetingSpeaker,
    std::shared_ptr<avsCommon::sdkInterfaces::meeting::MeetingClientInterface> meetingClient,
    std::shared_ptr<avsCommon::sdkInterfaces::calendar::CalendarClientInterface> calendarClient,
#endif
#ifdef ENABLE_COMMS_AUDIO_PROXY
    std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface> commsMediaPlayer,
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface> commsSpeaker,
    std::shared_ptr<alexaClientSDK::avsCommon::avs::AudioInputStream> sharedDataStream,
#endif
    std::shared_ptr<EqualizerRuntimeSetup> equalizerRuntimeSetup,
    std::shared_ptr<avsCommon::sdkInterfaces::audio::AudioFactoryInterface> audioFactory,
    std::shared_ptr<avsCommon::sdkInterfaces::AuthDelegateInterface> authDelegate,
    std::shared_ptr<alexaClientSDK::acsdkAlerts::storage::AlertStorageInterface> alertStorage,
    std::shared_ptr<certifiedSender::MessageStorageInterface> messageStorage,
    std::shared_ptr<alexaClientSDK::acsdkNotificationsInterfaces::NotificationsStorageInterface> notificationsStorage,
    std::shared_ptr<settings::storage::DeviceSettingStorageInterface> deviceSettingStorage,
    std::shared_ptr<alexaClientSDK::acsdkBluetooth::BluetoothStorageInterface> bluetoothStorage,
    std::shared_ptr<avsCommon::sdkInterfaces::storage::MiscStorageInterface> miscStorage,
    std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::DialogUXStateObserverInterface>>
        alexaDialogStateObservers,
    std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::ConnectionStatusObserverInterface>>
        connectionObservers,
    std::shared_ptr<avsCommon::sdkInterfaces::InternetConnectionMonitorInterface> internetConnectionMonitor,
    std::shared_ptr<avsCommon::sdkInterfaces::CapabilitiesDelegateInterface> capabilitiesDelegate,
    std::shared_ptr<avsCommon::sdkInterfaces::ContextManagerInterface> contextManager,
    std::shared_ptr<alexaClientSDK::acl::TransportFactoryInterface> transportFactory,
    std::shared_ptr<avsCommon::sdkInterfaces::LocaleAssetsManagerInterface> localeAssetsManager,
    std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::bluetooth::BluetoothDeviceConnectionRuleInterface>>
        enabledConnectionRules,
    std::shared_ptr<avsCommon::sdkInterfaces::SystemTimeZoneInterface> systemTimezone,
    avsCommon::sdkInterfaces::softwareInfo::FirmwareVersion firmwareVersion,
    bool sendSoftwareInfoOnConnected,
    std::shared_ptr<avsCommon::sdkInterfaces::SoftwareInfoSenderObserverInterface> softwareInfoSenderObserver,
    std::unique_ptr<avsCommon::sdkInterfaces::bluetooth::BluetoothDeviceManagerInterface> bluetoothDeviceManager,
    std::shared_ptr<avsCommon::sdkInterfaces::AVSGatewayManagerInterface> avsGatewayManager,
    std::shared_ptr<avsCommon::sdkInterfaces::PowerResourceManagerInterface> powerResourceManager,
    std::shared_ptr<avsCommon::sdkInterfaces::diagnostics::DiagnosticsInterface> diagnostics,
    const std::shared_ptr<ExternalCapabilitiesBuilderInterface>& externalCapabilitiesBuilder,
    std::shared_ptr<avsCommon::sdkInterfaces::ChannelVolumeFactoryInterface> channelVolumeFactory,
    std::shared_ptr<alexaSmartScreenSDK::smartScreenSDKInterfaces::VisualStateProviderInterface> visualStateProvider,
    const std::string& APLMaxVersion) {
    if (!audioFactory) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullAudioFactory"));
        return false;
    }

    if (!speakMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullSpeakMediaPlayer"));
        return false;
    }

    if (!audioMediaPlayerFactory) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullAudioMediaPlayerFactory"));
        return false;
    }

    if (!alertsMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullAlertsMediaPlayer"));
        return false;
    }

    if (!notificationsMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullNotificationsMediaPlayer"));
        return false;
    }

    if (!bluetoothMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullBluetoothMediaPlayer"));
        return false;
    }

    if (!ringtoneMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullRingtoneMediaPlayer"));
        return false;
    }

    if (!systemSoundMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullSystemSoundMediaPlayer"));
        return false;
    }

    if (!authDelegate) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullAuthDelegate"));
        return false;
    }

    if (!capabilitiesDelegate) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullCapabilitiesDelegate"));
        return false;
    }

    if (!deviceSettingStorage) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullDeviceSettingStorage"));
        return false;
    }

    if (!contextManager) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullContextManager"));
        return false;
    }

    if (!transportFactory) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullTransportFactory"));
        return false;
    }

    if (!avsGatewayManager) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullAVSGatewayManager"));
        return false;
    }

    if (!channelVolumeFactory) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullChannelVolumeFactory"));
        return false;
    }

    if (!visualStateProvider) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullvisualStateProvider"));
        return false;
    }

    if (APLMaxVersion.empty()) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "emptyAPLVersion"));
        return false;
    }

    m_avsGatewayManager = avsGatewayManager;

    m_dialogUXStateAggregator = std::make_shared<avsCommon::avs::DialogUXStateAggregator>();

    for (auto observer : alexaDialogStateObservers) {
        m_dialogUXStateAggregator->addObserver(observer);
    }

    /*
     * Creating the Attachment Manager - This component deals with managing
     * attachments and allows for readers and
     * writers to be created to handle the attachment.
     */
    auto attachmentManager = std::make_shared<avsCommon::avs::attachment::AttachmentManager>(
        avsCommon::avs::attachment::AttachmentManager::AttachmentType::IN_PROCESS);

    /*
     * Creating the message router - This component actually maintains the
     * connection to AVS over HTTP2. It is created
     * using the auth delegate, which provides authorization to connect to AVS,
     * and the attachment manager, which helps
     * ACL write attachments received from AVS.
     */
    m_messageRouter = std::make_shared<acl::MessageRouter>(authDelegate, attachmentManager, transportFactory);

    if (!internetConnectionMonitor) {
        ACSDK_CRITICAL(LX("initializeFailed").d("reason", "internetConnectionMonitor was nullptr"));
        return false;
    }
    m_internetConnectionMonitor = internetConnectionMonitor;

    /*
     * Creating the connection manager - This component is the overarching
     * connection manager that glues together all
     * the other networking components into one easy-to-use component.
     */
    m_connectionManager = acl::AVSConnectionManager::create(
        m_messageRouter, false, connectionObservers, {m_dialogUXStateAggregator}, internetConnectionMonitor);
    if (!m_connectionManager) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateConnectionManager"));
        return false;
    }

    /*
     * Creating our certified sender - this component guarantees that messages
     * given to it (expected to be JSON
     * formatted AVS Events) will be sent to AVS.  This nicely decouples strict
     * message sending from components which
     * require an Event be sent, even in conditions when there is no active AVS
     * connection.
     */
    m_certifiedSender = certifiedSender::CertifiedSender::create(
        m_connectionManager, m_connectionManager, messageStorage, customerDataManager);
    if (!m_certifiedSender) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateCertifiedSender"));
        return false;
    }

    /*
     * Creating the Exception Sender - This component helps the SDK send
     * exceptions when it is unable to handle a
     * directive sent by AVS. For that reason, the Directive Sequencer and each
     * Capability Agent will need this
     * component.
     */
    m_exceptionSender = avsCommon::avs::ExceptionEncounteredSender::create(m_connectionManager);
    if (!m_exceptionSender) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateExceptionSender"));
        return false;
    }

    /*
     * Creating the Directive Sequencer - This is the component that deals with
     * the sequencing and ordering of
     * directives sent from AVS and forwarding them along to the appropriate
     * Capability Agent that deals with
     * directives in that Namespace/Name.
     */
    m_directiveSequencer = adsl::DirectiveSequencer::create(m_exceptionSender);
    if (!m_directiveSequencer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateDirectiveSequencer"));
        return false;
    }

    /*
     * Creating the Message Interpreter - This component takes care of converting
     * ACL messages to Directives for the
     * Directive Sequencer to process. This essentially "glues" together the ACL
     * and ADSL.
     */
    auto messageInterpreter =
        std::make_shared<adsl::MessageInterpreter>(m_exceptionSender, m_directiveSequencer, attachmentManager);

    m_connectionManager->addMessageObserver(messageInterpreter);

    /*
     * Creating the Registration Manager - This component is responsible for
     * implementing any customer registration
     * operation such as login and logout
     */
    m_registrationManager = std::make_shared<registrationManager::RegistrationManager>(
        m_directiveSequencer, m_connectionManager, customerDataManager);

    // Create endpoint related objects.
    m_contextManager = contextManager;
    m_endpointManager = EndpointRegistrationManager::create(
        m_directiveSequencer, capabilitiesDelegate, m_deviceInfo.getDefaultEndpointId());
    if (!m_endpointManager) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "endpointRegistrationManagerCreateFailed"));
        return false;
    }

    m_deviceSettingStorage = deviceSettingStorage;
    if (!m_deviceSettingStorage->open()) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "deviceSettingStorageOpenFailed"));
        return false;
    }

    /*
     * Creating the DoNotDisturb Capability Agent.
     *
     * TODO(ACSDK-2279): Keep this here till we can inject DND setting into the DND CA.
     */
    m_dndCapabilityAgent = capabilityAgents::doNotDisturb::DoNotDisturbCapabilityAgent::create(
        m_exceptionSender, m_connectionManager, m_deviceSettingStorage);

    if (!m_dndCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateDNDCapabilityAgent"));
        return false;
    }

    addConnectionObserver(m_dndCapabilityAgent);

    DeviceSettingsManagerBuilder settingsManagerBuilder{
        m_deviceSettingStorage, m_connectionManager, m_connectionManager, customerDataManager};
    settingsManagerBuilder.withDoNotDisturbSetting(m_dndCapabilityAgent)
        .withAlarmVolumeRampSetting()
        .withWakeWordConfirmationSetting()
        .withSpeechConfirmationSetting()
        .withTimeZoneSetting(systemTimezone);

    if (localeAssetsManager->getDefaultSupportedWakeWords().empty()) {
        settingsManagerBuilder.withLocaleSetting(localeAssetsManager);
    } else {
        settingsManagerBuilder.withLocaleAndWakeWordsSettings(localeAssetsManager);
    }

    m_deviceSettingsManager = settingsManagerBuilder.build();
    if (!m_deviceSettingsManager) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "createDeviceSettingsManagerFailed"));
        return false;
    }

    m_deviceTimeZoneOffset = settingsManagerBuilder.getDeviceTimezoneOffset();

    /*
     * Creating the Audio Activity Tracker - This component is responsibly for
     * reporting the audio channel focus
     * information to AVS.
     */
    m_audioActivityTracker = afml::AudioActivityTracker::create(contextManager);

    /**
     *  Interrupt Model object
     */
    auto interruptModel = alexaClientSDK::afml::interruptModel::InterruptModel::create(
        alexaClientSDK::avsCommon::utils::configuration::ConfigurationNode::getRoot()[INTERRUPT_MODEL_CONFIG_KEY]);

    // Read audioChannels configuration from config file
    std::vector<afml::FocusManager::ChannelConfiguration> audioVirtualChannelConfiguration;
    if (!afml::FocusManager::ChannelConfiguration::readChannelConfiguration(
            AUDIO_CHANNEL_CONFIG_KEY, &audioVirtualChannelConfiguration)) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToReadAudioChannelConfiguration"));
        return false;
    }

    /*
     * Creating the Focus Manager - This component deals with the management of
     * layered audio focus across various
     * components. It handles granting access to Channels as well as pushing
     * different "Channels" to foreground,
     * background, or no focus based on which other Channels are active and the
     * priorities of those Channels. Each
     * Capability Agent will require the Focus Manager in order to request access
     * to the Channel it wishes to play on.
     */
    m_audioFocusManager =
        std::make_shared<afml::FocusManager>(
            afml::FocusManager::getDefaultAudioChannels(),
            m_audioActivityTracker,
            audioVirtualChannelConfiguration,
            interruptModel);

#ifdef ENABLE_CAPTIONS
    /*
     * Creating the Caption Manager - This component deals with handling captioned content.
     */
    auto webvttParser = captions::LibwebvttParserAdapter::getInstance();
    m_captionManager = captions::CaptionManager::create(webvttParser);
#endif

    /*
     * Creating the User Inactivity Monitor - This component is responsibly for
     * updating AVS of user inactivity as
     * described in the System Interface of AVS.
     */
    m_userInactivityMonitor =
        capabilityAgents::system::UserInactivityMonitor::create(m_connectionManager, m_exceptionSender);
    if (!m_userInactivityMonitor) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateUserInactivityMonitor"));
        return false;
    }

    m_systemSoundPlayer = applicationUtilities::systemSoundPlayer::SystemSoundPlayer::create(
        systemSoundMediaPlayer, audioFactory->systemSounds());

    auto wakeWordConfirmationSetting =
        settingsManagerBuilder.getSetting<settings::DeviceSettingsIndex::WAKEWORD_CONFIRMATION>();
    auto speechConfirmationSetting =
        settingsManagerBuilder.getSetting<settings::DeviceSettingsIndex::SPEECH_CONFIRMATION>();
    auto wakeWordsSetting = settingsManagerBuilder.getSetting<settings::DeviceSettingsIndex::WAKE_WORDS>();
/*
 * Creating the Audio Input Processor - This component is the Capability Agent
 * that implements the SpeechRecognizer interface of AVS.
 */
#ifdef ENABLE_OPUS
    m_audioInputProcessor = capabilityAgents::aip::AudioInputProcessor::create(
        m_directiveSequencer,
        m_connectionManager,
        contextManager,
        m_audioFocusManager,
        m_dialogUXStateAggregator,
        m_exceptionSender,
        m_userInactivityMonitor,
        m_systemSoundPlayer,
        localeAssetsManager,
        wakeWordConfirmationSetting,
        speechConfirmationSetting,
        wakeWordsSetting,
        std::make_shared<speechencoder::SpeechEncoder>(std::make_shared<speechencoder::OpusEncoderContext>()),
        capabilityAgents::aip::AudioProvider::null(),
        powerResourceManager);
#else
    m_audioInputProcessor = capabilityAgents::aip::AudioInputProcessor::create(
        m_directiveSequencer,
        m_connectionManager,
        contextManager,
        m_audioFocusManager,
        m_dialogUXStateAggregator,
        m_exceptionSender,
        m_userInactivityMonitor,
        m_systemSoundPlayer,
        localeAssetsManager,
        wakeWordConfirmationSetting,
        speechConfirmationSetting,
        wakeWordsSetting,
        nullptr,
        capabilityAgents::aip::AudioProvider::null(),
        powerResourceManager);
#endif

    if (!m_audioInputProcessor) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateAudioInputProcessor"));
        return false;
    }

    m_audioInputProcessor->addObserver(m_dialogUXStateAggregator);

    std::shared_ptr<avsCommon::utils::metrics::MetricRecorderInterface> metricRecorder;
#ifdef ACSDK_ENABLE_METRICS_RECORDING
    auto recorderImpl = std::make_shared<alexaClientSDK::metrics::implementations::MetricRecorder>();
    recorderImpl->addSink(std::move(metricSinkInterface));
    metricRecorder = recorderImpl;
#endif

    /*
     * Creating the Speech Synthesizer - This component is the Capability Agent
     * that implements the SpeechSynthesizer
     * interface of AVS.
     */
#ifdef ENABLE_CAPTIONS
    m_speechSynthesizer = capabilityAgents::speechSynthesizer::SpeechSynthesizer::create(
        speakMediaPlayer,
        m_connectionManager,
        m_audioFocusManager,
        contextManager,
        m_exceptionSender,
        metricRecorder,
        m_dialogUXStateAggregator,
        m_captionManager,
        powerResourceManager);
#else
    m_speechSynthesizer = capabilityAgents::speechSynthesizer::SpeechSynthesizer::create(
        speakMediaPlayer,
        m_connectionManager,
        m_audioFocusManager,
        contextManager,
        m_exceptionSender,
        metricRecorder,
        m_dialogUXStateAggregator,
        nullptr,
        powerResourceManager);
#endif

    if (!m_speechSynthesizer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateSpeechSynthesizer"));
        return false;
    }

    m_speechSynthesizer->addObserver(m_dialogUXStateAggregator);

    /*
     * Creating the PlaybackController Capability Agent - This component is the
     * Capability Agent that implements the
     * PlaybackController interface of AVS.
     */
    m_playbackController =
        capabilityAgents::playbackController::PlaybackController::create(contextManager, m_connectionManager);
    if (!m_playbackController) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreatePlaybackController"));
        return false;
    }

    /*
     * Creating the PlaybackRouter - This component routes a playback button press
     * to the active handler.
     * The default handler is @c PlaybackController.
     */
    m_playbackRouter = capabilityAgents::playbackController::PlaybackRouter::create(m_playbackController);
    if (!m_playbackRouter) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreatePlaybackRouter"));
        return false;
    }

    // create @c SpeakerInterfaces for each @c Type
    std::vector<std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface>> allAvsSpeakers{
        speakSpeaker, systemSoundSpeaker};
    std::vector<std::shared_ptr<avsCommon::sdkInterfaces::SpeakerInterface>> allAlertSpeakers{
        alertsSpeaker, notificationsSpeaker};
    // parse additional Speakers into the right speaker list.
    for (const auto& it : additionalSpeakers) {
        if (avsCommon::sdkInterfaces::ChannelVolumeInterface::Type::AVS_SPEAKER_VOLUME == it.first) {
            allAvsSpeakers.push_back(it.second);
        } else if (avsCommon::sdkInterfaces::ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME == it.first) {
            allAlertSpeakers.push_back(it.second);
        }
    }

#ifdef ENABLE_PCC
    allAvsSpeakers.push_back(phoneSpeaker);
#endif

#ifdef ENABLE_MCC
    allAvsSpeakers.push_back(meetingSpeaker);
#endif

#ifdef ENABLE_COMMS_AUDIO_PROXY
    allAvsSpeakers.push_back(commsSpeaker);
#endif

    // create @c ChannelVolumeInterface instances for all @c SpeakerInterface instances
    std::vector<std::shared_ptr<avsCommon::sdkInterfaces::ChannelVolumeInterface>> allAvsChannelVolumeInterfaces,
        allAlertChannelVolumeInterfaces, allChannelVolumeInterfaces;

    // create allAvsChannelVolumeInterfaces using allAvsSpeakers
    for (auto& it : allAvsSpeakers) {
        allAvsChannelVolumeInterfaces.push_back(channelVolumeFactory->createChannelVolumeInterface(it));
    }

    // create @c ChannelVolumeInterface for audioSpeakers (later used by AudioPlayer,MRM CapabilityAgents)
    std::vector<std::shared_ptr<avsCommon::sdkInterfaces::ChannelVolumeInterface>> audioChannelVolumeInterfaces;
    for (auto& it : audioSpeakers) {
        audioChannelVolumeInterfaces.push_back(channelVolumeFactory->createChannelVolumeInterface(it));
    }
    allAvsChannelVolumeInterfaces.insert(
        allAvsChannelVolumeInterfaces.end(), audioChannelVolumeInterfaces.begin(), audioChannelVolumeInterfaces.end());

    // create @c ChannelVolumeInterface for bluetoothSpeaker (later used by Bluetooth CapabilityAgent)
    std::shared_ptr<avsCommon::sdkInterfaces::ChannelVolumeInterface> bluetoothChannelVolumeInterface =
        channelVolumeFactory->createChannelVolumeInterface(bluetoothSpeaker);
    allAvsChannelVolumeInterfaces.push_back(bluetoothChannelVolumeInterface);

    // create @c ChannelVolumeInterface for ringtoneSpeaker
    std::shared_ptr<avsCommon::sdkInterfaces::ChannelVolumeInterface> ringtoneChannelVolumeInterface =
        channelVolumeFactory->createChannelVolumeInterface(ringtoneSpeaker);
    allAvsChannelVolumeInterfaces.push_back(ringtoneChannelVolumeInterface);

    // create @c ChannelVolumeInterface for allAlertSpeakers
    for (auto& it : allAlertSpeakers) {
        allAlertChannelVolumeInterfaces.push_back(
            channelVolumeFactory->createChannelVolumeInterface(it, ChannelVolumeInterface::Type::AVS_ALERTS_VOLUME));
    }

    /*
     * Creating the SpeakerManager Capability Agent - This component is the
     * Capability Agent that implements the
     * Speaker interface of AVS.
     */
    allChannelVolumeInterfaces.insert(
        allChannelVolumeInterfaces.end(), allAvsChannelVolumeInterfaces.begin(), allAvsChannelVolumeInterfaces.end());
    allChannelVolumeInterfaces.insert(
        allChannelVolumeInterfaces.end(),
        allAlertChannelVolumeInterfaces.begin(),
        allAlertChannelVolumeInterfaces.end());

    m_speakerManager = capabilityAgents::speakerManager::SpeakerManager::create(
        allChannelVolumeInterfaces, contextManager, m_connectionManager, m_exceptionSender, metricRecorder);
    if (!m_speakerManager) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateSpeakerManager"));
        return false;
    }

    /*
     * Creating the Audio Player - This component is the Capability Agent that
     * implements the AudioPlayer
     * interface of AVS.
     */
    m_audioPlayer = alexaClientSDK::acsdkAudioPlayer::AudioPlayer::AudioPlayer::create(
        std::move(audioMediaPlayerFactory),
        m_connectionManager,
        m_audioFocusManager,
        contextManager,
        m_exceptionSender,
        m_playbackRouter,
        audioChannelVolumeInterfaces,
#ifdef ENABLE_CAPTIONS
        m_captionManager,
#else
        nullptr,
#endif
        metricRecorder);

    if (!m_audioPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateAudioPlayer"));
        return false;
    }

    auto alertRenderer = alexaClientSDK::acsdkAlerts::renderer::Renderer::create(alertsMediaPlayer);
    if (!alertRenderer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateAlarmRenderer"));
        return false;
    }

    /*
     * Creating the Alerts Capability Agent - This component is the Capability
     * Agent that implements the Alerts
     * interface of AVS.
     */
    m_alertsCapabilityAgent = alexaClientSDK::acsdkAlerts::AlertsCapabilityAgent::create(
        m_connectionManager,
        m_connectionManager,
        m_certifiedSender,
        m_audioFocusManager,
        m_speakerManager,
        contextManager,
        m_exceptionSender,
        alertStorage,
        audioFactory->alerts(),
        alertRenderer,
        customerDataManager,
        settingsManagerBuilder.getSetting<settings::ALARM_VOLUME_RAMP>(),
        m_deviceSettingsManager,
        metricRecorder);
    if (!m_alertsCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateAlertsCapabilityAgent"));
        return false;
    }

    addConnectionObserver(m_dialogUXStateAggregator);

    m_notificationsRenderer =
        alexaClientSDK::acsdkNotifications::NotificationRenderer::create(notificationsMediaPlayer, m_audioFocusManager);
    if (!m_notificationsRenderer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateNotificationsRenderer"));
        return false;
    }

    /*
     * Creating the Notifications Capability Agent - This component is the
     * Capability Agent that implements the
     * Notifications interface of AVS.
     */
    m_notificationsCapabilityAgent = alexaClientSDK::acsdkNotifications::NotificationsCapabilityAgent::create(
        notificationsStorage,
        m_notificationsRenderer,
        contextManager,
        m_exceptionSender,
        audioFactory->notifications(),
        customerDataManager);
    if (!m_notificationsCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateNotificationsCapabilityAgent"));
        return false;
    }

    m_interactionCapabilityAgent = capabilityAgents::interactionModel::InteractionModelCapabilityAgent::create(
        m_directiveSequencer, m_exceptionSender);
    if (!m_interactionCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateInteractionModelCapabilityAgent"));
        return false;
    }
    /*
     * Listen to when Request Processing Started (RPS) directive is received
     * to enter the THINKING mode (Interaction Model 1.1).
     */
    m_interactionCapabilityAgent->addObserver(m_dialogUXStateAggregator);

#ifdef ENABLE_PCC
    /*
     * Creating the PhoneCallController - This component is the Capability Agent
     * that implements the
     * PhoneCallController interface of AVS
     */
    m_phoneCallControllerCapabilityAgent = capabilityAgents::phoneCallController::PhoneCallController::create(
        contextManager, m_connectionManager, phoneCaller, phoneSpeaker, m_audioFocusManager, m_exceptionSender);
    if (!m_phoneCallControllerCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreatePhoneCallControllerCapabilityAgent"));
    }
#endif

#ifdef ENABLE_MCC
    /*
     * Creating the MeetingClientController - This component is the Capability Agent that implements the
     * MeetingClientController interface of AVS
     */
    m_meetingClientControllerCapabilityAgent =
        capabilityAgents::meetingClientController::MeetingClientController::create(
            contextManager,
            m_connectionManager,
            meetingClient,
            calendarClient,
            m_speakerManager,
            m_audioFocusManager,
            m_exceptionSender);
    if (!m_meetingClientControllerCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateMeetingClientControllerCapabilityAgent"));
    }
#endif

#ifdef ENABLE_COMMS
    if (!ringtoneMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "nullRingtoneMediaPlayer"));
        return false;
    }

    auto sipUserAgent = std::make_shared<capabilityAgents::callManager::SipUserAgent>();

    if (!capabilityAgents::callManager::CallManager::create(
            sipUserAgent,
            ringtoneMediaPlayer,
            m_connectionManager,
            contextManager,
            m_audioFocusManager,
            m_exceptionSender,
            audioFactory->communications(),
            nullptr,
            m_speakerManager)) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateCallManager"));
        return false;
    }

    m_callManager = capabilityAgents::callManager::CallManager::getInstance();
    addConnectionObserver(m_callManager);

#ifdef ENABLE_COMMS_AUDIO_PROXY
    auto acquireAudioInputStream = [sharedDataStream]() -> std::shared_ptr<avsCommon::avs::AudioInputStream> {
        return sharedDataStream;
    };
    auto relinquishAudioInputStream = [](std::shared_ptr<avsCommon::avs::AudioInputStream> stream) {
        // Nothing to release
    };
    m_callAudioDeviceProxy = capabilityAgents::callManager::CallAudioDeviceProxy::create(
        commsMediaPlayer, commsSpeaker, acquireAudioInputStream, relinquishAudioInputStream);
    m_callManager->addObserver(m_callAudioDeviceProxy);
#endif
#endif

    /*
     * Creating the ExternalMediaPlayer CA - This component is the Capability
     * Agent that implements the
     * ExternalMediaPlayer interface of AVS.
     */
    /*
     * Creating the ExternalMediaPlayer CA - This component is the Capability
     * Agent that implements the ExternalMediaPlayer interface of AVS.
     */
    alexaClientSDK::capabilityAgents::externalMediaPlayer::ExternalMediaPlayer::AdapterSpeakerMap
        externalMusicProviderVolumeInterfaces;
    for (auto& it : externalMusicProviderSpeakers) {
        externalMusicProviderVolumeInterfaces[it.first] = channelVolumeFactory->createChannelVolumeInterface(it.second);
    }

    m_externalMediaPlayer = capabilityAgents::externalMediaPlayer::ExternalMediaPlayer::create(
        externalMusicProviderMediaPlayers,
        externalMusicProviderVolumeInterfaces,
        adapterCreationMap,
        m_speakerManager,
        m_connectionManager,
        m_certifiedSender,
        m_audioFocusManager,
        contextManager,
        m_exceptionSender,
        m_playbackRouter,
        metricRecorder);
    if (!m_externalMediaPlayer) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateExternalMediaPlayer"));
        return false;
    }

#ifdef ENABLE_MRM
    // Creating the MRM (Multi-Room-Music) Capability Agent.

#ifdef ENABLE_MRM_STANDALONE_APP
    auto mrmHandler = capabilityAgents::mrm::mrmHandler::MRMHandlerProxy::create(
        m_connectionManager,
        m_connectionManager,
        m_directiveSequencer,
        m_userInactivityMonitor,
        contextManager,
        m_audioFocusManager,
        m_speakerManager);
#else
    auto mrmHandler = capabilityAgents::mrm::mrmHandler::MRMHandler::create(
        m_connectionManager,
        m_connectionManager,
        m_directiveSequencer,
        m_userInactivityMonitor,
        contextManager,
        m_audioFocusManager,
        m_speakerManager,
        m_deviceInfo.getDeviceSerialNumber());
#endif  // ENABLE_MRM_STANDALONE_APP

    if (!mrmHandler) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "Unable to create mrmHandler."));
        return false;
    }

    m_mrmCapabilityAgent = capabilityAgents::mrm::MRMCapabilityAgent::create(
        std::move(mrmHandler), m_speakerManager, m_userInactivityMonitor, m_exceptionSender);

    if (!m_mrmCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateMRMCapabilityAgent"));
        return false;
    }

    /// Reminder that this needs to be called after m_callManager is set up or
    /// or it won't do anything
    /// MRM cares about the comms state because it needs to:
    /// 1) Not start music on devices already in a call
    /// 2) Stop music on a cluster if a member enters a call
    addCallStateObserver(m_mrmCapabilityAgent);
#endif  // ENABLE_MRM

    /*
     * Creating the Visual Activity Tracker - This component is responsibly for reporting the visual channel focus
     * information to AVS.
     */
    m_visualActivityTracker = afml::VisualActivityTracker::create(contextManager);

    /*
     * Creating the Visual Focus Manager - This component deals with the management of visual focus across various
     * components. It handles granting access to Channels as well as pushing different "Channels" to foreground,
     * background, or no focus based on which other Channels are active and the priorities of those Channels. Each
     * Capability Agent will require the Focus Manager in order to request access to the Channel it wishes to play
     * on.
     */
    m_visualFocusManager =
        std::make_shared<afml::FocusManager>(afml::FocusManager::getDefaultVisualChannels(), m_visualActivityTracker);

    /*
     * Creating the AlexaPresentation Capability Agent - This component is the Capability Agent that
     * implements the AlexaPresentation and AlexaPresentation.APL AVS interface.
     */
    m_alexaPresentation =
        alexaSmartScreenSDK::smartScreenCapabilityAgents::alexaPresentation::AlexaPresentation::create(
            m_visualFocusManager,
            m_exceptionSender,
            metricRecorder,
            m_connectionManager,
            contextManager,
            visualStateProvider);
    if (!m_alexaPresentation) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateAlexaPresentationCapabilityAgent"));
        return false;
    }
    m_dialogUXStateAggregator->addObserver(m_alexaPresentation);
    m_alexaPresentation->setAPLMaxVersion(APLMaxVersion);

    std::unordered_set<std::shared_ptr<avsCommon::sdkInterfaces::RenderPlayerInfoCardsProviderInterface>>
        renderPlayerInfoCardsProviders = {m_audioPlayer, m_externalMediaPlayer};

#ifdef ENABLE_MRM
    renderPlayerInfoCardsProviders.insert(m_mrmCapabilityAgent);
#endif

    /*
     * Creating the TemplateRuntime Capability Agent - This component is the Capability Agent that
     * implements the TemplateRuntime interface.
     */
    m_templateRuntime = alexaSmartScreenSDK::smartScreenCapabilityAgents::templateRuntime::TemplateRuntime::create(
        renderPlayerInfoCardsProviders, m_visualFocusManager, m_exceptionSender);
    if (!m_templateRuntime) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateTemplateRuntimeCapabilityAgent"));
        return false;
    }
    m_dialogUXStateAggregator->addObserver(m_templateRuntime);
    addAlexaPresentationObserver(m_templateRuntime);

    /*
     * Creating the VisualCharacteristics Capability Agent - This component is the Capability Agent that
     * publish Alexa.Display, Alexa.Display.Window, Alexa.InteractionMode,Alexa.Presentation.APL.Video interfaces.
     */
    m_visualCharacteristics =
        alexaSmartScreenSDK::smartScreenCapabilityAgents::visualCharacteristics::VisualCharacteristics::create(
            contextManager);
    if (!m_visualCharacteristics) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateVisualCharacteristicsCapabilityAgent"));
        return false;
    }

    /*
     * Creating the Equalizer Capability Agent and related implementations if
     * enabled
     */

    m_equalizerRuntimeSetup = equalizerRuntimeSetup;
    if (nullptr != m_equalizerRuntimeSetup) {
        auto equalizerController = equalizer::EqualizerController::create(
            equalizerRuntimeSetup->getModeController(),
            equalizerRuntimeSetup->getConfiguration(),
            equalizerRuntimeSetup->getStorage());

        if (!equalizerController) {
            ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateEqualizerController"));
            return false;
        }

        m_equalizerCapabilityAgent = capabilityAgents::equalizer::EqualizerCapabilityAgent::create(
            equalizerController,
            capabilitiesDelegate,
            equalizerRuntimeSetup->getStorage(),
            customerDataManager,
            m_exceptionSender,
            contextManager,
            m_connectionManager);
        if (!m_equalizerCapabilityAgent) {
            ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateEqualizerCapabilityAgent"));
            return false;
        }

        m_equalizerController = equalizerController;
        // Register equalizers
        for (auto& equalizer : m_equalizerRuntimeSetup->getAllEqualizers()) {
            equalizerController->registerEqualizer(equalizer);
        }

        // Add all equalizer controller listeners
        for (auto& listener : m_equalizerRuntimeSetup->getAllEqualizerControllerListeners()) {
            equalizerController->addListener(listener);
        }
    } else {
        ACSDK_DEBUG3(LX(__func__).m("Equalizer is disabled"));
    }

    /*
     * Creating the TimeZone Handler - This component is responsible for handling directives related to time zones.
     */
    auto timezoneHandler = capabilityAgents::system::TimeZoneHandler::create(
        settingsManagerBuilder.getSetting<settings::TIMEZONE>(), m_exceptionSender);
    if (!timezoneHandler) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateTimeZoneHandler"));
        return false;
    }

    /*
     * Creating the Locale Handler - This component is responsible for handling directives related to locales.
     */
    auto localeHandler = capabilityAgents::system::LocaleHandler::create(
        m_exceptionSender, settingsManagerBuilder.getSetting<settings::DeviceSettingsIndex::LOCALE>());
    if (!localeHandler) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateLocaleHandler"));
        return false;
    }

    /*
     * Creating the ReportState Handler - This component is responsible for the ReportState directives.
     */
    auto reportGenerator = capabilityAgents::system::StateReportGenerator::create(
        m_deviceSettingsManager, settingsManagerBuilder.getConfigurations());
    if (!reportGenerator.hasValue()) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateStateReportGenerator"));
        return false;
    }

    std::vector<capabilityAgents::system::StateReportGenerator> reportGenerators{{reportGenerator.value()}};
    auto reportStateHandler = capabilityAgents::system::ReportStateHandler::create(
        customerDataManager,
        m_exceptionSender,
        m_connectionManager,
        m_connectionManager,
        miscStorage,
        reportGenerators);
    if (!reportStateHandler) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateReportStateHandler"));
        return false;
    }

    /*
     * Creating the SystemCapabilityProvider - This component is responsible for
     * publishing information about the System
     * capability agent.
     */
    auto systemCapabilityProvider = capabilityAgents::system::SystemCapabilityProvider::create(localeAssetsManager);
    if (!systemCapabilityProvider) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateSystemCapabilityProvider"));
        return false;
    }

#ifdef ENABLE_REVOKE_AUTH
    /*
     * Creating the RevokeAuthorizationHandler - This component is responsible for
     * handling RevokeAuthorization
     * directives from AVS to notify the client to clear out authorization and
     * re-enter the registration flow.
     */
    m_revokeAuthorizationHandler = capabilityAgents::system::RevokeAuthorizationHandler::create(m_exceptionSender);
    if (!m_revokeAuthorizationHandler) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateRevokeAuthorizationHandler"));
        return false;
    }
#endif

    if (avsCommon::sdkInterfaces::softwareInfo::isValidFirmwareVersion(firmwareVersion)) {
        auto tempSender = capabilityAgents::system::SoftwareInfoSender::create(
            firmwareVersion,
            sendSoftwareInfoOnConnected,
            m_softwareInfoSenderObservers,
            m_connectionManager,
            m_connectionManager,
            m_exceptionSender);
        if (tempSender) {
            std::lock_guard<std::mutex> lock(m_softwareInfoSenderMutex);
            m_softwareInfoSender = tempSender;
        } else {
            ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateSoftwareInfoSender"));
            return false;
        }
    }

    if (bluetoothDeviceManager) {
        ACSDK_DEBUG5(LX(__func__).m("Creating Bluetooth CA"));

        // Create a temporary pointer to the eventBus inside of
        // bluetoothDeviceManager so that
        // the unique ptr for bluetoothDeviceManager can be moved.
        auto eventBus = bluetoothDeviceManager->getEventBus();

        auto bluetoothMediaInputTransformer =
            alexaClientSDK::acsdkBluetooth::BluetoothMediaInputTransformer::create(eventBus, m_playbackRouter);

        /*
         * Creating the Bluetooth Capability Agent - This component is responsible
         * for handling directives from AVS
         * regarding bluetooth functionality.
         */
        m_bluetooth = alexaClientSDK::acsdkBluetooth::Bluetooth::create(
            contextManager,
            m_audioFocusManager,
            m_connectionManager,
            m_exceptionSender,
            std::move(bluetoothStorage),
            std::move(bluetoothDeviceManager),
            std::move(eventBus),
            bluetoothMediaPlayer,
            customerDataManager,
            enabledConnectionRules,
            bluetoothChannelVolumeInterface,
            bluetoothMediaInputTransformer);
    } else {
        ACSDK_DEBUG5(LX("bluetoothCapabilityAgentDisabled").d("reason", "nullBluetoothDeviceManager"));
    }

    m_apiGatewayCapabilityAgent =
        capabilityAgents::apiGateway::ApiGatewayCapabilityAgent::create(m_avsGatewayManager, m_exceptionSender);
    if (!m_apiGatewayCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateApiGatewayCapabilityAgent"));
        return false;
    }

    /*
     * Create the AlexaInterfaceMessageSender for use by endpoint-based capability agents.
     */
    m_alexaMessageSender =
        capabilityAgents::alexa::AlexaInterfaceMessageSender::create(m_contextManager, m_connectionManager);
    if (!m_alexaMessageSender) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateAlexaMessageSender"));
        return false;
    }

    /*
     * Create the AlexaInterfaceCapabilityAgent for the default endpoint.
     */
    m_alexaCapabilityAgent = capabilityAgents::alexa::AlexaInterfaceCapabilityAgent::create(
        m_deviceInfo, m_deviceInfo.getDefaultEndpointId(), m_exceptionSender, m_alexaMessageSender);
    if (!m_alexaCapabilityAgent) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToCreateAlexaCapabilityAgent"));
        return false;
    }

    // Add capabilitiesDelegate as an observer to EventProcessed messages.
    m_alexaCapabilityAgent->addEventProcessedObserver(capabilitiesDelegate);

    /**
     * Configure alexa client default endpoint.
     */
    m_defaultEndpointBuilder =
        EndpointBuilder::create(m_deviceInfo, m_contextManager, m_exceptionSender, m_alexaMessageSender);

    /*
     * Register capability agents and capability configurations.
     */
    m_defaultEndpointBuilder->withCapability(m_speechSynthesizer, m_speechSynthesizer);
    m_defaultEndpointBuilder->withCapability(m_audioPlayer, m_audioPlayer);
    m_defaultEndpointBuilder->withCapability(m_externalMediaPlayer, m_externalMediaPlayer);
    m_defaultEndpointBuilder->withCapability(m_audioInputProcessor, m_audioInputProcessor);
    m_defaultEndpointBuilder->withCapability(m_alertsCapabilityAgent, m_alertsCapabilityAgent);
    m_defaultEndpointBuilder->withCapability(m_apiGatewayCapabilityAgent, m_apiGatewayCapabilityAgent);
    m_defaultEndpointBuilder->withCapability(
        m_alexaCapabilityAgent->getCapabilityConfiguration(), m_alexaCapabilityAgent);
    m_defaultEndpointBuilder->withCapabilityConfiguration(m_audioActivityTracker);
#ifdef ENABLE_PCC
    if (m_phoneCallControllerCapabilityAgent) {
        m_defaultEndpointBuilder->withCapability(
            m_phoneCallControllerCapabilityAgent, m_phoneCallControllerCapabilityAgent);
    }
#endif

#ifdef ENABLE_MCC
    if (m_meetingClientControllerCapabilityAgent) {
        m_defaultEndpointBuilder->withCapability(
            m_meetingClientControllerCapabilityAgent, m_meetingClientControllerCapabilityAgent);
    }
#endif

    m_defaultEndpointBuilder->withCapability(m_speakerManager, m_speakerManager);

    m_defaultEndpointBuilder->withCapability(m_interactionCapabilityAgent, m_interactionCapabilityAgent);
    m_defaultEndpointBuilder->withCapability(m_alexaPresentation, m_alexaPresentation);
    m_defaultEndpointBuilder->withCapability(m_templateRuntime, m_templateRuntime);
    m_defaultEndpointBuilder->withCapabilityConfiguration(m_visualActivityTracker);

    m_defaultEndpointBuilder->withCapability(m_notificationsCapabilityAgent, m_notificationsCapabilityAgent);
    m_defaultEndpointBuilder->withCapability(m_interactionCapabilityAgent, m_interactionCapabilityAgent);

#ifdef ENABLE_COMMS
    // The CallManager is an optional component, so it may be nullptr.
    auto callManager = capabilityAgents::callManager::CallManager::getInstance();
    if (m_callManager && callManager) {
        m_defaultEndpointBuilder->withCapability(callManager, m_callManager);
    }
#endif

    if (m_bluetooth) {
        m_defaultEndpointBuilder->withCapability(m_bluetooth, m_bluetooth);
    }

    if (m_mrmCapabilityAgent) {
        m_defaultEndpointBuilder->withCapability(m_mrmCapabilityAgent, m_mrmCapabilityAgent);
    }

    if (m_equalizerCapabilityAgent) {
        m_defaultEndpointBuilder->withCapability(m_equalizerCapabilityAgent, m_equalizerCapabilityAgent);
    }

    m_defaultEndpointBuilder->withCapability(m_dndCapabilityAgent, m_dndCapabilityAgent);

    // System CA is split into multiple directive handlers.
    m_defaultEndpointBuilder->withCapabilityConfiguration(systemCapabilityProvider);
    if (!m_directiveSequencer->addDirectiveHandler(std::move(localeHandler)) ||
        !m_directiveSequencer->addDirectiveHandler(std::move(timezoneHandler)) ||
        !m_directiveSequencer->addDirectiveHandler(std::move(reportStateHandler)) ||
#ifdef ENABLE_REVOKE_AUTH
        !m_directiveSequencer->addDirectiveHandler(m_revokeAuthorizationHandler) ||
#endif
        !m_directiveSequencer->addDirectiveHandler(m_userInactivityMonitor)) {
        ACSDK_ERROR(LX("initializeFailed").d("reason", "unableToRegisterSystemDirectiveHandler"));
        return false;
    }

    if (softwareInfoSenderObserver) {
        m_softwareInfoSenderObservers.insert(softwareInfoSenderObserver);
    }
    if (m_callManager) {
        m_softwareInfoSenderObservers.insert(m_callManager);
    }

    if (!m_defaultEndpointBuilder->finishDefaultEndpointConfiguration()) {
        ACSDK_ERROR(LX("initializedFailed").d("reason", "defaultEndpointConfigurationFailed"));
        return false;
    }

    return true;
}

void SmartScreenClient::onCapabilitiesStateChange(
    CapabilitiesObserverInterface::State newState,
    CapabilitiesObserverInterface::Error newError,
    const std::vector<avsCommon::sdkInterfaces::endpoints::EndpointIdentifier>& addedOrUpdatedEndpoints,
    const std::vector<avsCommon::sdkInterfaces::endpoints::EndpointIdentifier>& deletedEndpoints) {
    if (CapabilitiesObserverInterface::State::SUCCESS == newState) {
        m_connectionManager->enable();
    }
}

void SmartScreenClient::connect(bool performReset) {
    if (performReset) {
        if (m_defaultEndpointBuilder) {
            // Build default endpoint.
            auto defaultEndpoint = m_defaultEndpointBuilder->buildDefaultEndpoint();
            if (!defaultEndpoint) {
                ACSDK_CRITICAL(LX("connectFailed").d("reason", "couldNotBuildDefaultEndpoint"));
                return;
            }

            // Register default endpoint. Only wait for immediate failures and return with a critical error, if so.
            // Otherwise, the default endpoint will be registered with AVS in the post-connect stage (once
            // m_connectionManager->enable() is called, below). We should not block on waiting for resultFuture
            // to be ready, since instead we rely on the post-connect operation and the onCapabilitiesStateChange
            // callback.
            auto resultFuture = m_endpointManager->registerEndpoint(std::move(defaultEndpoint));
            if ((resultFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)) {
                auto result = resultFuture.get();
                if (result != alexaClientSDK::endpoints::EndpointRegistrationManager::RegistrationResult::SUCCEEDED) {
                    ACSDK_CRITICAL(LX("connectFailed").d("reason", "registrationFailed").d("result", result));
                    return;
                }
            }
            m_defaultEndpointBuilder.reset();
        }
        // Ensure default endpoint registration is enqueued with @c EndpointRegistrationManager
        // before proceeding with connection. Otherwise, we risk a race condition where the post-connect operations
        // are created before the default endpoint is enqueued for publishing to AVS.
        m_endpointManager->waitForPendingRegistrationsToEnqueue();
        m_avsGatewayManager->setAVSGatewayAssigner(m_connectionManager);
    }
    m_connectionManager->enable();
}

void SmartScreenClient::disconnect() {
    m_connectionManager->disable();
}

std::string SmartScreenClient::getAVSGateway() {
    return m_connectionManager->getAVSGateway();
}

// === Workaround start ===
/*
 * In order to support multi-turn interactions SDK processes SpeechSynthesizer audio context in special way. This
 * leads to skill context not been cleared on cloud side when we ask for exit. In order to fix that we should grab
 * DIALOG channel by interface processed in normal way and proceed as before.
 * More global AVS C++ SDK solution to be implemented later.
 */
/// Interface name to use for focus requests.
static const std::string APL_INTERFACE("Alexa.Presentation.APL");

void SmartScreenClient::forceExit() {
    ACSDK_DEBUG5(LX(__func__).m("Force Exit"));
    m_audioFocusManager->acquireChannel(
        avsCommon::sdkInterfaces::FocusManagerInterface::DIALOG_CHANNEL_NAME, shared_from_this(), APL_INTERFACE);
}

void SmartScreenClient::onFocusChanged(
    alexaClientSDK::avsCommon::avs::FocusState newFocus,
    alexaClientSDK::avsCommon::avs::MixingBehavior behavior) {
    if (newFocus == alexaClientSDK::avsCommon::avs::FocusState::FOREGROUND) {
        stopForegroundActivity();
        m_audioInputProcessor->resetState();
        clearCard();
    }
}
// === Workaround end ===
void SmartScreenClient::clearCard() {
    m_alexaPresentation->clearCard();
    m_templateRuntime->clearCard();
}

void SmartScreenClient::stopForegroundActivity() {
    m_audioFocusManager->stopForegroundActivity();
}

void SmartScreenClient::localStopActiveAlert() {
    m_alertsCapabilityAgent->onLocalStop();
}

void SmartScreenClient::addAlexaDialogStateObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::DialogUXStateObserverInterface> observer) {
    m_dialogUXStateAggregator->addObserver(observer);
}

void SmartScreenClient::removeAlexaDialogStateObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::DialogUXStateObserverInterface> observer) {
    m_dialogUXStateAggregator->removeObserver(observer);
}

void SmartScreenClient::addMessageObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::MessageObserverInterface> observer) {
    m_connectionManager->addMessageObserver(observer);
}

void SmartScreenClient::removeMessageObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::MessageObserverInterface> observer) {
    m_connectionManager->removeMessageObserver(observer);
}

void SmartScreenClient::addConnectionObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::ConnectionStatusObserverInterface> observer) {
    m_connectionManager->addConnectionStatusObserver(observer);
}

void SmartScreenClient::removeConnectionObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::ConnectionStatusObserverInterface> observer) {
    m_connectionManager->removeConnectionStatusObserver(observer);
}

void SmartScreenClient::addInternetConnectionObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::InternetConnectionObserverInterface> observer) {
    m_internetConnectionMonitor->addInternetConnectionObserver(observer);
}

void SmartScreenClient::removeInternetConnectionObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::InternetConnectionObserverInterface> observer) {
    m_internetConnectionMonitor->removeInternetConnectionObserver(observer);
}

void SmartScreenClient::addAlertsObserver(
    std::shared_ptr<alexaClientSDK::acsdkAlertsInterfaces::AlertObserverInterface> observer) {
    m_alertsCapabilityAgent->addObserver(observer);
}

void SmartScreenClient::removeAlertsObserver(
    std::shared_ptr<alexaClientSDK::acsdkAlertsInterfaces::AlertObserverInterface> observer) {
    m_alertsCapabilityAgent->removeObserver(observer);
}

void SmartScreenClient::addAudioPlayerObserver(
    std::shared_ptr<alexaClientSDK::acsdkAudioPlayerInterfaces::AudioPlayerObserverInterface> observer) {
    m_audioPlayer->addObserver(observer);
}

void SmartScreenClient::removeAudioPlayerObserver(
    std::shared_ptr<alexaClientSDK::acsdkAudioPlayerInterfaces::AudioPlayerObserverInterface> observer) {
    m_audioPlayer->removeObserver(observer);
}

void SmartScreenClient::addTemplateRuntimeObserver(
    std::shared_ptr<alexaSmartScreenSDK::smartScreenSDKInterfaces::TemplateRuntimeObserverInterface> observer) {
    m_templateRuntime->addObserver(observer);
}

void SmartScreenClient::removeTemplateRuntimeObserver(
    std::shared_ptr<alexaSmartScreenSDK::smartScreenSDKInterfaces::TemplateRuntimeObserverInterface> observer) {
    m_templateRuntime->removeObserver(observer);
}

void SmartScreenClient::TemplateRuntimeDisplayCardCleared() {
    m_templateRuntime->displayCardCleared();
}

void SmartScreenClient::addNotificationsObserver(
    std::shared_ptr<alexaClientSDK::acsdkNotificationsInterfaces::NotificationsObserverInterface> observer) {
    m_notificationsCapabilityAgent->addObserver(observer);
}

void SmartScreenClient::removeNotificationsObserver(
    std::shared_ptr<alexaClientSDK::acsdkNotificationsInterfaces::NotificationsObserverInterface> observer) {
    m_notificationsCapabilityAgent->removeObserver(observer);
}

void SmartScreenClient::addExternalMediaPlayerObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::externalMediaPlayer::ExternalMediaPlayerObserverInterface> observer) {
    m_externalMediaPlayer->addObserver(observer);
}

void SmartScreenClient::removeExternalMediaPlayerObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::externalMediaPlayer::ExternalMediaPlayerObserverInterface> observer) {
    m_externalMediaPlayer->removeObserver(observer);
}

#ifdef ENABLE_CAPTIONS
void SmartScreenClient::addCaptionPresenter(std::shared_ptr<captions::CaptionPresenterInterface> presenter) {
    if (m_captionManager) {
        m_captionManager->setCaptionPresenter(presenter);
    }
}

void SmartScreenClient::setCaptionMediaPlayers(
    const std::vector<std::shared_ptr<avsCommon::utils::mediaPlayer::MediaPlayerInterface>>& mediaPlayers) {
    if (m_captionManager) {
        m_captionManager->setMediaPlayers(mediaPlayers);
    }
}
#endif

void SmartScreenClient::addBluetoothDeviceObserver(
    std::shared_ptr<alexaClientSDK::acsdkBluetoothInterfaces::BluetoothDeviceObserverInterface> observer) {
    if (!m_bluetooth) {
        ACSDK_DEBUG5(LX(__func__).m("bluetooth is disabled, not adding observer"));
        return;
    }
    m_bluetooth->addObserver(observer);
}

void SmartScreenClient::removeBluetoothDeviceObserver(
    std::shared_ptr<alexaClientSDK::acsdkBluetoothInterfaces::BluetoothDeviceObserverInterface> observer) {
    if (!m_bluetooth) {
        return;
    }
    m_bluetooth->removeObserver(observer);
}

#ifdef ENABLE_REVOKE_AUTH
void SmartScreenClient::addRevokeAuthorizationObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::RevokeAuthorizationObserverInterface> observer) {
    if (!m_revokeAuthorizationHandler) {
        ACSDK_ERROR(LX("addRevokeAuthorizationObserver").d("reason", "revokeAuthorizationNotSupported"));
        return;
    }
    m_revokeAuthorizationHandler->addObserver(observer);
}

void SmartScreenClient::removeRevokeAuthorizationObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::RevokeAuthorizationObserverInterface> observer) {
    if (!m_revokeAuthorizationHandler) {
        ACSDK_ERROR(LX("removeRevokeAuthorizationObserver").d("reason", "revokeAuthorizationNotSupported"));
        return;
    }
    m_revokeAuthorizationHandler->removeObserver(observer);
}
#endif

std::shared_ptr<settings::DeviceSettingsManager> SmartScreenClient::getSettingsManager() {
    return m_deviceSettingsManager;
}

std::shared_ptr<avsCommon::sdkInterfaces::PlaybackRouterInterface> SmartScreenClient::getPlaybackRouter() const {
    return m_playbackRouter;
}

std::shared_ptr<alexaSmartScreenSDK::smartScreenCapabilityAgents::alexaPresentation::AlexaPresentation>
SmartScreenClient::getAlexaPresentation() const {
    return m_alexaPresentation;
}

std::shared_ptr<avsCommon::sdkInterfaces::FocusManagerInterface> SmartScreenClient::getAudioFocusManager() const {
    return m_audioFocusManager;
}

std::shared_ptr<avsCommon::sdkInterfaces::FocusManagerInterface> SmartScreenClient::getVisualFocusManager() const {
    return m_visualFocusManager;
}

std::shared_ptr<registrationManager::RegistrationManager> SmartScreenClient::getRegistrationManager() {
    return m_registrationManager;
}

std::shared_ptr<equalizer::EqualizerController> SmartScreenClient::getEqualizerController() {
    return m_equalizerController;
}

void SmartScreenClient::addSpeakerManagerObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerManagerObserverInterface> observer) {
    m_speakerManager->addSpeakerManagerObserver(observer);
}

void SmartScreenClient::removeSpeakerManagerObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::SpeakerManagerObserverInterface> observer) {
    m_speakerManager->removeSpeakerManagerObserver(observer);
}

std::shared_ptr<avsCommon::sdkInterfaces::SpeakerManagerInterface> SmartScreenClient::getSpeakerManager() {
    return m_speakerManager;
}

bool SmartScreenClient::setFirmwareVersion(avsCommon::sdkInterfaces::softwareInfo::FirmwareVersion firmwareVersion) {
    {
        std::lock_guard<std::mutex> lock(m_softwareInfoSenderMutex);

        if (!m_softwareInfoSender) {
            m_softwareInfoSender = capabilityAgents::system::SoftwareInfoSender::create(
                firmwareVersion,
                true,
                m_softwareInfoSenderObservers,
                m_connectionManager,
                m_connectionManager,
                m_exceptionSender);
            if (m_softwareInfoSender) {
                return true;
            }

            ACSDK_ERROR(LX("setFirmwareVersionFailed").d("reason", "unableToCreateSoftwareInfoSender"));
            return false;
        }
    }

    return m_softwareInfoSender->setFirmwareVersion(firmwareVersion);
}

std::future<bool> SmartScreenClient::notifyOfWakeWord(
    capabilityAgents::aip::AudioProvider wakeWordAudioProvider,
    avsCommon::avs::AudioInputStream::Index beginIndex,
    avsCommon::avs::AudioInputStream::Index endIndex,
    std::string keyword,
    std::chrono::steady_clock::time_point startOfSpeechTimestamp,
    std::shared_ptr<const std::vector<char>> KWDMetadata) {
    ACSDK_DEBUG5(LX(__func__).d("keyword", keyword).d("connected", m_connectionManager->isConnected()));

    if (!m_connectionManager->isConnected()) {
        std::promise<bool> ret;
        if (capabilityAgents::aip::AudioInputProcessor::KEYWORD_TEXT_STOP == keyword) {
            // Alexa Stop uttered while offline
            ACSDK_INFO(LX("notifyOfWakeWord").d("action", "localStop").d("reason", "stopUtteredWhileNotConnected"));
            stopForegroundActivity();

            // Returning as interaction handled
            ret.set_value(true);
            return ret.get_future();
        } else {
            // Ignore Alexa wake word while disconnected
            ACSDK_INFO(LX("notifyOfWakeWord").d("action", "ignoreAlexaWakeWord").d("reason", "networkDisconnected"));

            // Returning as interaction not handled
            ret.set_value(false);
            return ret.get_future();
        }
    }

    return m_audioInputProcessor->recognize(
        wakeWordAudioProvider,
        capabilityAgents::aip::Initiator::WAKEWORD,
        startOfSpeechTimestamp,
        beginIndex,
        endIndex,
        keyword,
        KWDMetadata);
}

std::future<bool> SmartScreenClient::notifyOfTapToTalk(
    capabilityAgents::aip::AudioProvider tapToTalkAudioProvider,
    avsCommon::avs::AudioInputStream::Index beginIndex,
    std::chrono::steady_clock::time_point startOfSpeechTimestamp) {
    return m_audioInputProcessor->recognize(
        tapToTalkAudioProvider, capabilityAgents::aip::Initiator::TAP, startOfSpeechTimestamp, beginIndex);
}

std::future<bool> SmartScreenClient::notifyOfHoldToTalkStart(
    capabilityAgents::aip::AudioProvider holdToTalkAudioProvider,
    std::chrono::steady_clock::time_point startOfSpeechTimestamp) {
    return m_audioInputProcessor->recognize(
        holdToTalkAudioProvider, capabilityAgents::aip::Initiator::PRESS_AND_HOLD, startOfSpeechTimestamp);
}

std::future<bool> SmartScreenClient::notifyOfHoldToTalkEnd() {
    return m_audioInputProcessor->stopCapture();
}

std::future<bool> SmartScreenClient::notifyOfTapToTalkEnd() {
    return m_audioInputProcessor->stopCapture();
}

void SmartScreenClient::addCallStateObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::CallStateObserverInterface> observer) {
    if (m_callManager) {
        m_callManager->addObserver(observer);
    }
}

void SmartScreenClient::removeCallStateObserver(
    std::shared_ptr<avsCommon::sdkInterfaces::CallStateObserverInterface> observer) {
    if (m_callManager) {
        m_callManager->removeObserver(observer);
    }
}

std::unique_ptr<EndpointBuilderInterface> SmartScreenClient::createEndpointBuilder() {
    return EndpointBuilder::create(m_deviceInfo, m_contextManager, m_exceptionSender, m_alexaMessageSender);
}
std::shared_ptr<EndpointBuilderInterface> SmartScreenClient::getDefaultEndpointBuilder() {
    return m_defaultEndpointBuilder;
}

bool SmartScreenClient::isCommsEnabled() {
    return (m_callManager != nullptr);
}

void SmartScreenClient::acceptCommsCall() {
    if (m_callManager) {
        m_callManager->acceptCall();
    }
}

void SmartScreenClient::sendDtmf(avsCommon::sdkInterfaces::CallManagerInterface::DTMFTone dtmfTone) {
    if (m_callManager) {
        m_callManager->sendDtmf(dtmfTone);
    }
}

void SmartScreenClient::stopCommsCall() {
    if (m_callManager) {
        m_callManager->stopCall();
    }
}

void SmartScreenClient::addAlexaPresentationObserver(
    std::shared_ptr<alexaSmartScreenSDK::smartScreenSDKInterfaces::AlexaPresentationObserverInterface> observer) {
    if (!m_alexaPresentation) {
        ACSDK_ERROR(LX("addAlexaPresentationObserverFailed").d("reason", "guiNotSupported"));
        return;
    }
    m_alexaPresentation->addObserver(observer);
}

void SmartScreenClient::removeAlexaPresentationObserver(
    std::shared_ptr<alexaSmartScreenSDK::smartScreenSDKInterfaces::AlexaPresentationObserverInterface> observer) {
    if (!m_alexaPresentation) {
        ACSDK_ERROR(LX("removeAlexaPresentationObserverFailed").d("reason", "guiNotSupported"));
        return;
    }
    m_alexaPresentation->removeObserver(observer);
}

void SmartScreenClient::sendUserEvent(const std::string& payload) {
    m_alexaPresentation->sendUserEvent(payload);
}

void SmartScreenClient::sendDataSourceFetchRequestEvent(const std::string& type, const std::string& payload) {
    m_alexaPresentation->sendDataSourceFetchRequestEvent(type, payload);
}

void SmartScreenClient::sendRuntimeErrorEvent(const std::string& payload) {
    m_alexaPresentation->sendRuntimeErrorEvent(payload);
}

void SmartScreenClient::handleVisualContext(uint64_t token, std::string payload) {
    m_alexaPresentation->onVisualContextAvailable(token, payload);
}

void SmartScreenClient::handleRenderDocumentResult(std::string token, bool result, std::string error) {
    m_alexaPresentation->processRenderDocumentResult(token, result, error);
}

void SmartScreenClient::handleExecuteCommandsResult(std::string token, bool result, std::string error) {
    m_alexaPresentation->processExecuteCommandsResult(token, result, error);
}

void SmartScreenClient::handleActivityEvent(
    const std::string& source,
    alexaSmartScreenSDK::smartScreenSDKInterfaces::ActivityEvent event,
    bool isAlexaPresentationPresenting) {
    if (isAlexaPresentationPresenting) {
        m_alexaPresentation->processActivityEvent(source, event);
    } else {
        m_templateRuntime->processActivityEvent(source, event);
    }
}

void SmartScreenClient::setDocumentIdleTimeout(std::chrono::milliseconds timeout) {
    m_alexaPresentation->setDocumentIdleTimeout(timeout);
}

void SmartScreenClient::clearAllExecuteCommands() {
    m_alexaPresentation->clearAllExecuteCommands();
}

void SmartScreenClient::setDeviceWindowState(const std::string& payload) {
    m_visualCharacteristics->setDeviceWindowState(payload);
}

void SmartScreenClient::addSpeechSynthesizerObserver(
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::SpeechSynthesizerObserverInterface> observer) {
    if (!m_speechSynthesizer) {
        ACSDK_ERROR(LX("addSpeechSynthesizerObserverFailed").d("reason", "speechSynthesizerNotSupported"));
        return;
    }
    m_speechSynthesizer->addObserver(observer);
};

void SmartScreenClient::removeSpeechSynthesizerObserver(
    std::shared_ptr<alexaClientSDK::avsCommon::sdkInterfaces::SpeechSynthesizerObserverInterface> observer) {
    if (!m_speechSynthesizer) {
        ACSDK_ERROR(LX("addSpeechSynthesizerObserverFailed").d("reason", "speechSynthesizerNotSupported"));
        return;
    }
    m_speechSynthesizer->removeObserver(observer);
}

std::chrono::milliseconds SmartScreenClient::getDeviceTimezoneOffset() {
    return m_deviceTimeZoneOffset;
}

void SmartScreenClient::handleRenderComplete(bool isAlexaPresentationPresenting) {
    if (isAlexaPresentationPresenting) {
        m_alexaPresentation->recordRenderComplete();
    }
}

void SmartScreenClient::handleDropFrameCount(uint64_t dropFrameCount, bool isAlexaPresentationPresenting) {
    if (isAlexaPresentationPresenting) {
        m_alexaPresentation->recordDropFrameCount(dropFrameCount);
    }
}

void SmartScreenClient::handleAPLEvent(APLClient::AplRenderingEvent event, bool isAlexaPresentationPresenting) {
    if (isAlexaPresentationPresenting) {
        m_alexaPresentation->recordAPLEvent(event);
    }
}

SmartScreenClient::~SmartScreenClient() {
    ACSDK_DEBUG3(LX(__func__));

    if (m_directiveSequencer) {
        ACSDK_DEBUG5(LX("DirectiveSequencerShutdown"));
        m_directiveSequencer->shutdown();
    }
    if (m_speakerManager) {
        ACSDK_DEBUG5(LX("SpeakerManagerShutdown"));
        m_speakerManager->shutdown();
    }
    if (m_alexaPresentation) {
        ACSDK_DEBUG5(LX("AlexaPresentationShutdown"));
        m_alexaPresentation->shutdown();
    }
    if (m_templateRuntime) {
        ACSDK_DEBUG5(LX("TemplateRuntimeShutdown"));
        m_templateRuntime->shutdown();
    }
    if (m_audioInputProcessor) {
        ACSDK_DEBUG5(LX("AIPShutdown"));
        m_audioInputProcessor->shutdown();
    }
    if (m_audioPlayer) {
        ACSDK_DEBUG5(LX("AudioPlayerShutdown"));
        m_audioPlayer->shutdown();
    }
    if (m_externalMediaPlayer) {
        ACSDK_DEBUG5(LX("ExternalMediaPlayerShutdown"));
        m_externalMediaPlayer->shutdown();
    }
    if (m_speechSynthesizer) {
        ACSDK_DEBUG5(LX("SpeechSynthesizerShutdown"));
        m_speechSynthesizer->shutdown();
    }
    if (m_alertsCapabilityAgent) {
        ACSDK_DEBUG5(LX("AlertsShutdown"));
        m_alertsCapabilityAgent->shutdown();
    }
    if (m_playbackController) {
        ACSDK_DEBUG5(LX("PlaybackControllerShutdown"));
        m_playbackController->shutdown();
    }
    if (m_softwareInfoSender) {
        ACSDK_DEBUG5(LX("SoftwareInfoShutdown"));
        m_softwareInfoSender->shutdown();
    }
    if (m_messageRouter) {
        ACSDK_DEBUG5(LX("MessageRouterShutdown."));
        m_messageRouter->shutdown();
    }
    if (m_connectionManager) {
        ACSDK_DEBUG5(LX("ConnectionManagerShutdown."));
        m_connectionManager->shutdown();
    }
    if (m_certifiedSender) {
        ACSDK_DEBUG5(LX("CertifiedSenderShutdown."));
        m_certifiedSender->shutdown();
    }
    if (m_audioActivityTracker) {
        ACSDK_DEBUG5(LX("AudioActivityTrackerShutdown."));
        m_audioActivityTracker->shutdown();
    }
    if (m_visualActivityTracker) {
        ACSDK_DEBUG5(LX("VisualActivityTrackerShutdown."));
        m_visualActivityTracker->shutdown();
    }
    if (m_playbackRouter) {
        ACSDK_DEBUG5(LX("PlaybackRouterShutdown."));
        m_playbackRouter->shutdown();
    }
    if (m_notificationsCapabilityAgent) {
        ACSDK_DEBUG5(LX("NotificationsShutdown."));
        m_notificationsCapabilityAgent->shutdown();
    }
    if (m_notificationsRenderer) {
        ACSDK_DEBUG5(LX("NotificationsRendererShutdown."));
        m_notificationsRenderer->shutdown();
    }
#ifdef ENABLE_CAPTIONS
    if (m_captionManager) {
        ACSDK_DEBUG5(LX("CaptionManagerShutdown."));
        m_captionManager->shutdown();
    }
#endif
    if (m_bluetooth) {
        ACSDK_DEBUG5(LX("BluetoothShutdown."));
        m_bluetooth->shutdown();
    }
    if (m_userInactivityMonitor) {
        ACSDK_DEBUG5(LX("UserInactivityMonitorShutdown."));
        m_userInactivityMonitor->shutdown();
    }
    if (m_mrmCapabilityAgent) {
        ACSDK_DEBUG5(LX("MRMCapabilityAgentShutdown"));
        removeCallStateObserver(m_mrmCapabilityAgent);
        m_mrmCapabilityAgent->shutdown();
    }
    if (m_callManager) {
        ACSDK_DEBUG5(LX("CallManagerShutdown."));
        m_callManager->shutdown();
    }

    if (m_apiGatewayCapabilityAgent) {
        ACSDK_DEBUG5(LX("CallApiGatewayCapabilityAgentShutdown."));
        m_apiGatewayCapabilityAgent->shutdown();
    }

    if (m_alexaMessageSender) {
        ACSDK_DEBUG5(LX("CallAlexaInterfaceMessageSenderShutdown."));
        m_alexaMessageSender->shutdown();
    }

#ifdef ENABLE_PCC
    if (m_phoneCallControllerCapabilityAgent) {
        ACSDK_DEBUG5(LX("PhoneCallControllerCapabilityAgentShutdown"));
        m_phoneCallControllerCapabilityAgent->shutdown();
    }
#endif
#ifdef ENABLE_MCC
    if (m_meetingClientControllerCapabilityAgent) {
        ACSDK_DEBUG5(LX("MeetingClientControllerCapabilityAgentShutdown"));
        m_meetingClientControllerCapabilityAgent->shutdown();
    }
#endif
    if (m_dndCapabilityAgent) {
        ACSDK_DEBUG5(LX("DNDCapabilityAgentShutdown"));
        removeConnectionObserver(m_dndCapabilityAgent);
        m_dndCapabilityAgent->shutdown();
    }

    if (m_visualCharacteristics) {
        m_visualCharacteristics->shutdown();
    }

    if (nullptr != m_equalizerCapabilityAgent) {
        for (auto& equalizer : m_equalizerRuntimeSetup->getAllEqualizers()) {
            m_equalizerController->unregisterEqualizer(equalizer);
        }
        for (auto& listener : m_equalizerRuntimeSetup->getAllEqualizerControllerListeners()) {
            m_equalizerController->removeListener(listener);
        }
        ACSDK_DEBUG5(LX("EqualizerCapabilityAgentShutdown"));
        m_equalizerCapabilityAgent->shutdown();
    }

    if (m_deviceSettingStorage) {
        ACSDK_DEBUG5(LX("CloseSettingStorage"));
        m_deviceSettingStorage->close();
    }

#ifdef ENABLE_COMMS_AUDIO_PROXY
    if (m_callManager) {
        m_callManager->removeObserver(m_callAudioDeviceProxy);
    }
#endif
}

}  // namespace smartScreenClient
}  // namespace alexaSmartScreenSDK
