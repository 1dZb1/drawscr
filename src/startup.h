#pragma once
#include <windows.h>
#include <string>

namespace drawscr {
enum class StartupState { Off, On, Other, Error };
class StartupStore {
  public:
    virtual ~StartupStore() = default;
    virtual StartupState query() = 0;
    virtual bool set(bool enabled, std::wstring& error) = 0;
    virtual LSTATUS error_code() const {
        return ERROR_SUCCESS;
    }
};

// Each operation addresses only Drawscr's own current-user Run value.
struct StartupValueOps {
    LSTATUS (*read)(void*, std::wstring&) = nullptr;
    LSTATUS (*write)(void*, const std::wstring&) = nullptr;
    LSTATUS (*remove)(void*) = nullptr;
    void* context = nullptr;
};
class RegistryStartupStore final : public StartupStore {
  public:
    explicit RegistryStartupStore(std::wstring executable, const StartupValueOps* ops = nullptr);
    StartupState query() override;
    bool set(bool enabled, std::wstring& error) override;
    LSTATUS error_code() const override {
        return last_error_;
    }
    const std::wstring& command() const {
        return command_;
    }

  private:
    std::wstring command_;
    StartupValueOps ops_;
    LSTATUS last_error_ = ERROR_SUCCESS;
};

// A click enables this executable unless it is already the active entry.
bool toggle_startup(StartupStore& store, StartupState& state, std::wstring& error);
}
