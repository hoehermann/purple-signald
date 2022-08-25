#pragma once

#if !(GLIB_CHECK_VERSION(2, 67, 3))
#define g_memdup2 g_memdup
#endif

#include <purple.h>

#if PURPLE_VERSION_CHECK(3, 0, 0)

#define purple_blist_get_root purple_blist_get_default_root
#define purple_conversation_get_data(conv, key) g_object_get_data(G_OBJECT(conv), key)
#define purple_conversation_set_data(conv, key, value) g_object_set_data(G_OBJECT(conv), key, value)
#define purple_conversation_find_chat_by_name(name, account) \
    purple_conversation_manager_find_chat(purple_conversation_manager_get_default(), account, name);
#define purple_conversation_find_im_by_name(name, account) \
    purple_conversation_manager_find_im(purple_conversation_manager_get_default(), account, name);
#define purple_debug_is_enabled() (FALSE) // TODO
#define purple_find_chat(pc, id) \
    purple_conversation_manager_find_chat_by_id(purple_conversation_manager_get_default(), purple_connection_get_account(pc), id);
#define purple_roomlist_unref(roomlist) g_object_unref(roomlist)
#define purple_timeout_remove g_source_remove
#define purple_timeout_add g_timeout_add
#define purple_timeout_add_seconds g_timeout_add_seconds

#else

#define purple_account_get_private_alias purple_account_get_alias
#define purple_action_menu_new purple_menu_action_new
#define purple_blist_find_group purple_find_group
#define purple_blist_find_buddies purple_find_buddies
#define purple_blist_find_buddy purple_find_buddy
#define purple_buddy_set_name purple_blist_rename_buddy
#define purple_cache_dir() purple_user_dir()
#define PURPLE_CHAT_CONVERSATION(conv) PURPLE_CONV_CHAT(conv)
#define purple_chat_conversation_add_user purple_conv_chat_add_user
#define purple_chat_conversation_clear_users purple_conv_chat_clear_users
#define purple_chat_conversation_get_users(chat) chat->users
#define purple_chat_conversation_set_nick purple_conv_chat_set_nick
#define purple_chat_conversation_set_topic purple_conv_chat_set_topic
#define PURPLE_CONNECTION_CONNECTED PURPLE_CONNECTED
#define PURPLE_CONNECTION_CONNECTING PURPLE_CONNECTING
#define PURPLE_CONNECTION_DISCONNECTED PURPLE_DISCONNECTED
#define PURPLE_CONNECTION_FLAG_NO_BGCOLOR PURPLE_CONNECTION_NO_BGCOLOR
#define PURPLE_CONNECTION_FLAG_NO_FONTSIZE PURPLE_CONNECTION_NO_FONTSIZE
#define purple_connection_error purple_connection_error_reason
#define purple_connection_get_flags(pc) ((pc)->flags)
#define purple_connection_set_flags(pc, f) ((pc)->flags = (f))
#define purple_conversation_find_chat_by_name(name, account) purple_find_conversation_with_account(PURPLE_CONV_TYPE_CHAT, name, account)
#define purple_conversation_find_im_by_name(who, account) purple_find_conversation_with_account(PURPLE_CONV_TYPE_IM, who, account)
#define purple_data_dir() purple_user_dir()
#define purple_date_format_full(tm) purple_date_format_long(tm)
#define purple_im_conversation_new(account, from) purple_conversation_new(PURPLE_CONV_TYPE_IM, account, from)
#define PURPLE_IS_CHAT PURPLE_BLIST_NODE_IS_CHAT
#define purple_protocol_got_user_status purple_prpl_got_user_status
#define purple_request_cpar_from_account(account) account, NULL, NULL
#define purple_roomlist_room_new(groupId, whatever) purple_roomlist_room_new(PURPLE_ROOMLIST_ROOMTYPE_ROOM, groupId, whatever);
#define purple_serv_got_alias serv_got_alias
#define purple_serv_got_im serv_got_im
#define purple_serv_got_joined_chat(pc, id, name) serv_got_joined_chat(pc, id, name)


#define PurpleActionMenu PurpleMenuAction
#define PurpleChatConversation PurpleConvChat
#define PurpleChatUserFlags PurpleConvChatBuddyFlags
#define PurpleProtocolChatEntry struct proto_chat_entry

#endif
