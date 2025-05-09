"""
Copyright (c) Contributors to the Open 3D Engine Project.
For complete copyright and license terms please see the LICENSE at the root of this distribution.

SPDX-License-Identifier: Apache-2.0 OR MIT
"""

from model import constants

NOTIFICATION_LOADING_MESSAGE: str = "Loading..."

ERROR_PAGE_OK_TEXT: str = "OK"

VIEW_EDIT_PAGE_CONFIG_FILE_TEXT: str = "Config File"
VIEW_EDIT_PAGE_CONFIG_LOCATION_TEXT: str = "Config Location:"
VIEW_EDIT_PAGE_ADD_ROW_TEXT: str = "Add Row"
VIEW_EDIT_PAGE_DELETE_ROW_TEXT: str = "Delete Row"
VIEW_EDIT_PAGE_SAVE_CHANGES_TEXT: str = "Save Changes"
VIEW_EDIT_PAGE_CANCEL_TEXT: str = "Cancel"
VIEW_EDIT_PAGE_CREATE_NEW_TEXT: str = "Create New"
VIEW_EDIT_PAGE_RESCAN_TEXT: str = "Rescan"

VIEW_EDIT_PAGE_CONFIG_FILES_PLACEHOLDER_TEXT: str = "Found {} config files"
VIEW_EDIT_PAGE_SEARCH_PLACEHOLDER_TEXT: str = "Search by Key Name, Type, Name/ID, Account ID or Region"
VIEW_EDIT_PAGE_IMPORT_RESOURCES_PLACEHOLDER_TEXT: str = "Import Additional Resources"
VIEW_EDIT_PAGE_CREATE_NEW_CONFIG_FILE_NO_DEFAULT_REGION_MESSAGE: str = \
    f"Resource mapping file {constants.RESOURCE_MAPPING_DEFAULT_CONFIG_FILE_NAME} is created"\
    f" with {constants.RESOURCE_MAPPING_DEFAULT_CONFIG_FILE_REGION} as the default region. "\
    f"See <a href=\"https://docs.o3de.org/docs/user-guide/gems/reference/aws/aws-core/configuring-credentials/\">"\
    f"<span style=\"color:#4A90E2;\">documentation</span></a> "\
    f"for configuring the AWS credentials and default region."

VIEW_EDIT_PAGE_SELECT_CONFIG_FILE_MESSAGE: str = "Please select the Config file you would like to view and modify..."
VIEW_EDIT_PAGE_NO_CONFIG_FILE_FOUND_MESSAGE: str = \
    "<p><b>Unable to locate a resource mapping config file.</b></p>"\
    "<p>We can either make this file for you or you can rescan your directory.</p>"
VIEW_EDIT_PAGE_SAVING_SUCCEED_MESSAGE: str = "Config file {} is saved successfully."

IMPORT_RESOURCES_PAGE_BACK_TEXT: str = "Back"
IMPORT_RESOURCES_PAGE_AWS_SEARCH_TYPE_TEXT: str = "AWS Resource Type"
IMPORT_RESOURCES_PAGE_SEARCH_TEXT: str = "Search"
IMPORT_RESOURCES_PAGE_IMPORT_TEXT: str = "Import"

IMPORT_RESOURCES_PAGE_SEARCH_PLACEHOLDER_TEXT: str = "Search for resources by Type or Name/ID"
IMPORT_RESOURCES_PAGE_AWS_SEARCH_TYPE_PLACEHOLDER_TEXT: str = "Please select"
