//
// PROJECT:         Aspia Remote Desktop
// FILE:            host/host.cc
// LICENSE:         Mozilla Public License Version 2.0
// PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
//

#include "host/host.h"
#include "host/host_session_console.h"
#include "host/host_user_list.h"
#include "crypto/secure_string.h"
#include "proto/auth_session.pb.h"
#include "protocol/message_serialization.h"

namespace aspia {

static const std::chrono::seconds kAuthTimeout{ 60 };

Host::Host(std::shared_ptr<NetworkChannel> channel, Delegate* delegate)
    : channel_(std::move(channel)),
      delegate_(delegate)
{
    channel_proxy_ = channel_->network_channel_proxy();

    channel_->StartChannel(std::bind(
        &Host::OnNetworkChannelStatusChange, this, std::placeholders::_1));
}

Host::~Host()
{
    channel_.reset();
}

bool Host::IsTerminatedSession() const
{
    return channel_proxy_->IsDiconnecting();
}

void Host::OnNetworkChannelStatusChange(NetworkChannel::Status status)
{
    if (status == NetworkChannel::Status::CONNECTED)
    {
        // If the authorization request is not received within the specified
        // time interval, the connection will be closed.
        auth_timer_.Start(kAuthTimeout,
                          std::bind(&NetworkChannelProxy::Disconnect,
                                    channel_proxy_));

        channel_proxy_->Receive(
            std::bind(&Host::DoAuthorize, this, std::placeholders::_1));
    }
    else
    {
        DCHECK(status == NetworkChannel::Status::DISCONNECTED);

        auth_timer_.Stop();
        session_.reset();

        delegate_->OnSessionTerminate();
    }
}

static proto::Status DoBasicAuthorization(const std::string& username,
                                          const std::string& password,
                                          proto::SessionType session_type)
{
    HostUserList user_list;

    if (!user_list.LoadFromStorage())
        return proto::Status::STATUS_ACCESS_DENIED;

    const int size = user_list.size();

    for (int i = 0; i < size; ++i)
    {
        const proto::HostUser& user = user_list.host_user(i);

        if (user.username() != username)
            continue;

        if (!user.enabled())
            return proto::Status::STATUS_ACCESS_DENIED;

        SecureString<std::string> password_hash;

        if (!HostUserList::CreatePasswordHash(password, password_hash))
            return proto::Status::STATUS_ACCESS_DENIED;

        if (user.password_hash() != password_hash)
            return proto::Status::STATUS_ACCESS_DENIED;

        if (!(user.session_types() & session_type))
            return proto::Status::STATUS_ACCESS_DENIED;

        return proto::Status::STATUS_SUCCESS;
    }

    return proto::Status::STATUS_ACCESS_DENIED;
}

void Host::DoAuthorize(std::unique_ptr<IOBuffer> buffer)
{
    // Authorization request received, stop the timer.
    auth_timer_.Stop();

    proto::auth::ClientToHost request;

    if (!ParseMessage(*buffer, request))
    {
        channel_proxy_->Disconnect();
        return;
    }

    proto::auth::HostToClient result;
    result.set_session_type(request.session_type());

    switch (request.method())
    {
        case proto::AuthMethod::AUTH_METHOD_BASIC:
            result.set_status(DoBasicAuthorization(request.username(),
                                                   request.password(),
                                                   request.session_type()));
            break;

        default:
            result.set_status(proto::Status::STATUS_ACCESS_DENIED);
            break;
    }

    SecureClearString(*request.mutable_username());
    SecureClearString(*request.mutable_password());

    channel_proxy_->Send(SerializeMessage<IOBuffer>(result), nullptr);

    if (result.status() == proto::Status::STATUS_SUCCESS)
    {
        switch (request.session_type())
        {
            case proto::SessionType::SESSION_TYPE_DESKTOP_MANAGE:
                session_ = HostSessionConsole::CreateForDesktopManage(channel_proxy_);
                break;

            case proto::SessionType::SESSION_TYPE_DESKTOP_VIEW:
                session_ = HostSessionConsole::CreateForDesktopView(channel_proxy_);
                break;

            case proto::SessionType::SESSION_TYPE_FILE_TRANSFER:
                session_ = HostSessionConsole::CreateForFileTransfer(channel_proxy_);
                break;

            case proto::SessionType::SESSION_TYPE_POWER_MANAGE:
                session_ = HostSessionConsole::CreateForPowerManage(channel_proxy_);
                break;

            default:
                LOG(ERROR) << "Unsupported session type: " << request.session_type();
                break;
        }

        if (session_)
            return;
    }

    channel_proxy_->Disconnect();
}

} // namespace aspia
