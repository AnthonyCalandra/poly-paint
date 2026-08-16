#include "file_dialog.h"

#include <ImGuiFileDialog.h>
#include <imgui.h>

#include <algorithm>
#include <string_view>

namespace poly_paint
{
    namespace
    {
        constexpr std::string_view dialog_key = "ChooseImageDialog";
        constexpr std::string_view image_filters =
            ".png,.jpg,.jpeg,.bmp,.tga,.gif,.psd,.hdr,.pic";

        [[nodiscard]] ImVec2 calculate_minimum_dialog_size()
        {
            const ImVec2 available = ImGui::GetMainViewport()->WorkSize;
            return {
                std::min(640.0f, available.x * 0.8f),
                std::min(400.0f, available.y * 0.8f)
            };
        }
    }

    void ImageFileDialog::open()
    {
        IGFD::FileDialogConfig config;
        config.path = ".";
        ImGuiFileDialog::Instance()->OpenDialog(
            dialog_key.data(), "Choose Image", image_filters.data(), config);
    }

    std::optional<std::string> ImageFileDialog::draw()
    {
        ImGuiFileDialog* const dialog = ImGuiFileDialog::Instance();
        if (!dialog->IsOpened(dialog_key.data()))
        {
            return std::nullopt;
        }

        const ImVec2 minimum_size = calculate_minimum_dialog_size();
        ImGui::SetNextWindowSize(minimum_size, ImGuiCond_Appearing);
        if (!dialog->Display(
                dialog_key.data(),
                ImGuiWindowFlags_NoCollapse,
                minimum_size))
        {
            return std::nullopt;
        }

        std::optional<std::string> selected_path;
        if (dialog->IsOk())
        {
            selected_path = dialog->GetFilePathName();
        }
        dialog->Close();
        return selected_path;
    }
}
