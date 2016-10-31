/*
* PROJECT:         Aspia Remote Desktop
* FILE:            base/service_control_win.cpp
* LICENSE:         See top-level directory
* PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
*/

#include "base/service_control_win.h"

// public
ServiceControl::ServiceControl(const WCHAR *service_short_name) :
    sc_manager_(nullptr),
    service_(nullptr)
{
    sc_manager_ = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);

    if (!sc_manager_)
    {
        LOG(ERROR) << "OpenSCManagerW() failed: " << GetLastError();
    }
    else
    {
        service_ = OpenServiceW(sc_manager_, service_short_name, SERVICE_ALL_ACCESS);
        if (!service_)
        {
            LOG(ERROR) << "OpenServiceW() failed: " << GetLastError();

            CloseServiceHandle(sc_manager_);
            sc_manager_ = nullptr;
        }
    }
}

// private
ServiceControl::ServiceControl(SC_HANDLE sc_manager, SC_HANDLE service) :
    sc_manager_(sc_manager),
    service_(service)
{
}

// public
ServiceControl::~ServiceControl()
{
    if (service_)
        CloseServiceHandle(service_);

    if (sc_manager_)
        CloseServiceHandle(sc_manager_);
}

// static
std::unique_ptr<ServiceControl> ServiceControl::AddService(const WCHAR *exec_path,
                                                           const WCHAR *service_full_name,
                                                           const WCHAR *service_short_name,
                                                           const WCHAR *service_description,
                                                           bool replace)
{
    // ��������� �������� �����
    SC_HANDLE sc_manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
    if (!sc_manager)
    {
        LOG(ERROR) << "OpenSCManagerW() failed: " << GetLastError();
        return std::unique_ptr<ServiceControl>();
    }

    SC_HANDLE service = nullptr;

    for (int retry_count = 0; retry_count < 3; ++retry_count)
    {
        // �������� ������� ������
        service = CreateServiceW(sc_manager,
                                 service_short_name,
                                 service_full_name,
                                 SERVICE_ALL_ACCESS,
                                 SERVICE_WIN32_OWN_PROCESS,
                                 SERVICE_AUTO_START,
                                 SERVICE_ERROR_NORMAL,
                                 exec_path,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr,
                                 nullptr);
        if (!service)
        {
            DWORD error = GetLastError();

            // ���� ������ ��� ���������� � ������ �������� ����������
            if (replace && error == ERROR_SERVICE_EXISTS)
            {
                // ��������� ������
                service = OpenServiceW(sc_manager, service_short_name, SERVICE_STOP | DELETE);
                if (service)
                {
                    SERVICE_STATUS status;

                    // ������������� ������
                    if (!ControlService(service, SERVICE_CONTROL_STOP, &status))
                    {
                        // ������ ����� � ���, �� ����������, �.�. ������ ����� ���� �� ��������
                        LOG(ERROR) << "ControlService() failed: " << GetLastError();
                    }

                    // ������� ������
                    if (DeleteService(service))
                    {
                        // ���� ������ ������� �������, �� ������� ������� ��������
                        CloseServiceHandle(service);
                        continue;
                    }
                    else
                    {
                        LOG(ERROR) << "DeleteService() failed: " << GetLastError();
                    }

                    CloseServiceHandle(service);
                    service = nullptr;
                }
                else
                {
                    LOG(ERROR) << "OpenServiceW() failed: " << GetLastError();
                }
            }
            else
            {
                LOG(ERROR) << "CreateServiceW() failed: " << error;
            }
        }
        else
        {
            SERVICE_DESCRIPTION description;
            description.lpDescription = const_cast<LPWSTR>(service_description);

            // ������������� �������� ������
            if (!ChangeServiceConfig2W(service,
                                       SERVICE_CONFIG_DESCRIPTION,
                                       &description))
            {
                LOG(ERROR) << "ChangeServiceConfig2W() failed: " << GetLastError();
            }
        }

        // ������� �� �����
        break;
    }

    if (!service)
    {
        CloseServiceHandle(sc_manager);
        return std::unique_ptr<ServiceControl>();
    }

    return std::unique_ptr<ServiceControl>(new ServiceControl(sc_manager, service));
}

// public
bool ServiceControl::IsValid() const
{
    return (sc_manager_ && service_);
}

// public
bool ServiceControl::Start() const
{
    if (!StartServiceW(service_, 0, nullptr))
    {
        LOG(ERROR) << "StartServiceW() failed: " << GetLastError();
        return false;
    }

    return true;
}

// public
bool ServiceControl::Stop() const
{
    SERVICE_STATUS status;

    if (!ControlService(service_, SERVICE_CONTROL_STOP, &status))
    {
        LOG(ERROR) << "ControlService() failed: " << GetLastError();
        return false;
    }

    return true;
}

// public
bool ServiceControl::Delete()
{
    if (!DeleteService(service_))
    {
        LOG(ERROR) << "DeleteService() failed: " << GetLastError();
        return false;
    }

    CloseServiceHandle(service_);
    CloseServiceHandle(sc_manager_);

    service_ = nullptr;
    sc_manager_ = nullptr;

    return true;
}
