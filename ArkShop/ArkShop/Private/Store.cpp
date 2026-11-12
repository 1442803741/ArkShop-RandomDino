#include <Store.h>

#include <Points.h>
#include <Permissions.h>

#include <cstdint>
#include <limits>
#include <random>
#include <vector>

#include "ArkShop.h"
#include "DBHelper.h"
#include "ShopLog.h"
#include "ArkShopUIHelper.h"
#include "Kits.h"

namespace ArkShop::Store
{
	namespace
	{
		struct DinoPoolCandidate
		{
			const nlohmann::json* entry;
			std::uint64_t weight;
			int min_level;
			int max_level;
		};

		bool ReadPositiveInt(const nlohmann::json& value, int& result)
		{
			if (!value.is_number_integer())
				return false;

			if (value.is_number_unsigned())
			{
				const auto number = value.get<std::uint64_t>();
				if (number == 0 || number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
					return false;
				result = static_cast<int>(number);
				return true;
			}

			const auto number = value.get<std::int64_t>();
			if (number < 1 || number > std::numeric_limits<int>::max())
				return false;

			result = static_cast<int>(number);
			return true;
		}

		bool SelectRandomDino(const nlohmann::json& item_entry, nlohmann::json& selected_entry, int& selected_level)
		{
			const auto pool_it = item_entry.find("Dinos");
			if (pool_it == item_entry.end())
			{
				selected_entry = item_entry;
				selected_level = item_entry.value("Level", 1);
				return true;
			}

			if (!pool_it->is_array() || pool_it->empty())
				return false;

			std::vector<DinoPoolCandidate> candidates;
			std::uint64_t total_weight = 0;

			for (const auto& candidate : *pool_it)
			{
				if (!candidate.is_object())
					continue;

				const auto blueprint_it = candidate.find("Blueprint");
				const auto min_it = candidate.find("MinLevel");
				const auto max_it = candidate.find("MaxLevel");
				if (blueprint_it == candidate.end() || !blueprint_it->is_string()
					|| blueprint_it->get<std::string>().empty()
					|| min_it == candidate.end() || max_it == candidate.end())
					continue;

				int min_level = 0;
				int max_level = 0;
				if (!ReadPositiveInt(*min_it, min_level) || !ReadPositiveInt(*max_it, max_level)
					|| max_level < min_level)
					continue;

				std::uint64_t weight = 1;
				const auto weight_it = candidate.find("Weight");
				if (weight_it != candidate.end())
				{
					if (!weight_it->is_number_integer())
						continue;

					if (weight_it->is_number_unsigned())
						weight = weight_it->get<std::uint64_t>();
					else
					{
						const auto signed_weight = weight_it->get<std::int64_t>();
						if (signed_weight <= 0)
							continue;
						weight = static_cast<std::uint64_t>(signed_weight);
					}
				}

				if (weight == 0 || total_weight > std::numeric_limits<std::uint64_t>::max() - weight)
					continue;

				total_weight += weight;
				candidates.push_back({ &candidate, weight, min_level, max_level });
			}

			if (candidates.empty() || total_weight == 0)
				return false;

			static thread_local std::mt19937_64 random_engine(std::random_device{}());
			std::uniform_int_distribution<std::uint64_t> weight_distribution(1, total_weight);
			const std::uint64_t roll = weight_distribution(random_engine);

			std::uint64_t cumulative_weight = 0;
			for (const auto& candidate : candidates)
			{
				cumulative_weight += candidate.weight;
				if (roll <= cumulative_weight)
				{
					selected_entry = *candidate.entry;
					std::uniform_int_distribution<int> level_distribution(candidate.min_level, candidate.max_level);
					selected_level = level_distribution(random_engine);
					return true;
				}
			}

			return false;
		}
	}
	/**
	 * \brief Buy an item from shop
	 */
	bool BuyItem(AShooterPlayerController* player_controller, const nlohmann::basic_json<>& item_entry, const FString& eos_id,
		int amount)
	{
		bool success = false;

		if (amount <= 0)
		{
			amount = 1;
		}

		const unsigned price = item_entry["Price"];
		const int final_price = price * amount;
		if (final_price <= 0)
		{
			return false;
		}

		const int points = Points::GetPoints(eos_id);

		if (points >= final_price && Points::SpendPoints(final_price, eos_id))
		{
			auto items_map = item_entry["Items"];
			for (const auto& item : items_map)
			{
				const float quality = item.value("Quality", 0);
				const bool force_blueprint = item.value("ForceBlueprint", false);
				const int default_amount = item.value("Amount", 1);
				std::string blueprint = item.value("Blueprint", "");
				int armor = item.value("Armor", 0);
				int durability = item.value("Durability", 0);
				int damage = item.value("Damage", 0);

				FString fblueprint(blueprint.c_str());

				TSubclassOf<UObject> archetype;
				UVictoryCore::StringReferenceToClass(&archetype, &fblueprint);
				auto itemClass = archetype.uClass;

				bool stacksInOne = false;
				if (itemClass)
				{
					UPrimalItem* itemCDO = static_cast<UPrimalItem*>(itemClass->GetDefaultObject(true));
					if (itemCDO)
					{
						stacksInOne = itemCDO->GetMaxItemQuantity(AsaApi::GetApiUtils().GetWorld()) <= 1;
					}
				}

				UPrimalInventoryComponent* playerInventory = player_controller->GetPlayerInventory();
				if (playerInventory)
				{
					if (stacksInOne || force_blueprint)
					{
						int loops = amount * default_amount;
						TArray<UPrimalItem*> out_items{};
						for (int i = 0; i < loops; ++i)
						{
							TSubclassOf<UPrimalItem> itemClassRef(itemClass);
							out_items.Add(
								UPrimalItem::AddNewItem(
									itemClassRef,
									playerInventory,
									false,
									false,
									quality,
									!force_blueprint,
									1,
									force_blueprint,
									0,
									false,
									nullptr,
									0,
									false,
									false,
									true,
									false,
									false,
									false,
									AsaApi::GetApiUtils().GetWorld()
								)
							);
						}

						ApplyItemStats(out_items, armor, durability, damage);
					}
					else
					{
						int totalAmount = amount * default_amount;
						playerInventory->IncrementItemTemplateQuantity(itemClass, totalAmount, true, force_blueprint, nullptr, nullptr, false, false, false, false, true, false, false, false, false, false, nullptr);
					}
				}
			}

			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("BoughtItem"));

			success = true;
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("NoPoints"));
		}

		return success;
	}

	/**
	* \brief Buy an unlockengram from shop
	*/
	bool UnlockEngram(AShooterPlayerController* player_controller, const nlohmann::basic_json<>& item_entry,
		const FString& eos_id)
	{
		bool success = false;
		const int price = item_entry["Price"];

		const int points = Points::GetPoints(eos_id);

		if (points >= price && Points::SpendPoints(price, eos_id))
		{
			auto items_map = item_entry["Items"];
			for (const auto& item : items_map)
			{
				const std::string blueprint = item.value("Blueprint", "");
				FString fblueprint(blueprint);

				auto* cheat_manager = static_cast<UShooterCheatManager*>(player_controller->CheatManagerField().Get());
				cheat_manager->UnlockEngram(&fblueprint);
			}

			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("BoughtItem"));

			success = true;
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("NoPoints"));
		}

		return success;
	}

	/**
	* \brief Buy an Command from shop
	*/
	bool BuyCommand(AShooterPlayerController* player_controller, const nlohmann::basic_json<>& item_entry, FString eos_id)
	{
		bool success = false;
		const int price = item_entry["Price"];

		const int points = Points::GetPoints(eos_id);

		if (points >= price && Points::SpendPoints(price, eos_id))
		{
			auto items_map = item_entry["Items"];
			for (const auto& item : items_map)
			{
				const std::string command = item.value("Command", "");

				const bool exec_as_admin = item.value("ExecuteAsAdmin", false);

				FString fcommand = fmt::format(
					command,
					fmt::arg("eosid", eos_id.ToString()),
					fmt::arg("eos_id", eos_id.ToString()),
					fmt::arg("playerid", AsaApi::GetApiUtils().GetPlayerID(player_controller)),
					fmt::arg("tribeid", AsaApi::GetApiUtils().GetTribeID(player_controller))
				).c_str();

				const bool was_admin = player_controller->bIsAdmin()();

				if (!was_admin && exec_as_admin)
					player_controller->bIsAdmin() = true;

				FString result;
				player_controller->ConsoleCommand(&result, &fcommand, false);

				if (!was_admin && exec_as_admin)
					player_controller->bIsAdmin() = false;
			}

			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("BoughtItem"));

			success = true;
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("NoPoints"));
		}

		return success;
	}

	/**
	* \brief Buy a dino from shop
	*/
	bool BuyDino(AShooterPlayerController* player_controller, const nlohmann::basic_json<>& item_entry, const FString& eos_id)
	{
		bool success = false;

		const int price = item_entry.value("Price", 0);

		// A legacy dino item uses its original fields. A Dinos array enables weighted random prizes.
		// Resolve the prize before charging points so an invalid pool never costs the player anything.
		nlohmann::json dino_entry;
		int level = 1;
		if (!SelectRandomDino(item_entry, dino_entry, level))
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				FString("Dino prize pool configuration is invalid."));
			return false;
		}

		const bool neutered = dino_entry.value("Neutered", item_entry.value("Neutered", false));
		std::string gender = dino_entry.value("Gender", item_entry.value("Gender", std::string("random")));
		std::string saddleblueprint = dino_entry.value("SaddleBlueprint", item_entry.value("SaddleBlueprint", std::string("")));
		std::string blueprint = dino_entry.value("Blueprint", item_entry.value("Blueprint", std::string("")));
		bool preventCryo = dino_entry.value("PreventCryo", item_entry.value("PreventCryo", false));
		const int stryderhead = dino_entry.value("StryderHead", item_entry.value("StryderHead", -1));
		const int stryderchest = dino_entry.value("StryderChest", item_entry.value("StryderChest", -1));
		nlohmann::json resourceoverrides = dino_entry.value("GachaResources",
			item_entry.value("GachaResources", nlohmann::json()));

		const int points = Points::GetPoints(eos_id);

		if (points >= price && Points::SpendPoints(price, eos_id))
		{
			success = ArkShop::GiveDino(player_controller, level, neutered, gender, blueprint, saddleblueprint, preventCryo, stryderhead, stryderchest, resourceoverrides);
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"), *GetText("NoPoints"));
			return success;
		}

		if (success)
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"), *GetText("BoughtDino"));
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"), *GetText("RefundError"));
			Points::AddPoints(price, eos_id); //refund
		}

		return success;
	}

	/**
	* \brief Buy a beacon from shop
	*/
	bool BuyBeacon(AShooterPlayerController* player_controller, const nlohmann::basic_json<>& item_entry,
		const FString& eos_id)
	{
		bool success = false;

		const int price = item_entry.value("Price", 0);
		std::string class_name = item_entry.value("ClassName", "");

		const int points = Points::GetPoints(eos_id);

		if (points >= price && Points::SpendPoints(price, eos_id))
		{
			FString fclass_name(class_name.c_str());

			auto* cheatManager = static_cast<UShooterCheatManager*>(player_controller->CheatManagerField().Get());
			cheatManager->Summon(&fclass_name);

			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("BoughtBeacon"));

			success = true;
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("NoPoints"));
		}

		return success;
	}

	/**
	* \brief Buy experience from shop
	*/
	bool BuyExperience(AShooterPlayerController* player_controller, const nlohmann::basic_json<>& item_entry,
		const FString& eos_id)
	{
		bool success = false;

		const int price = item_entry.value("Price", 0);
		const float amount = item_entry.value("Amount", 1);
		const bool give_to_dino = item_entry.value("GiveToDino", false);

		if (!give_to_dino && AsaApi::IApiUtils::IsRidingDino(player_controller))
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("RidingDino"));
			return false;
		}

		const int points = Points::GetPoints(eos_id);

		if (points >= price && Points::SpendPoints(price, eos_id))
		{
			player_controller->AddExperience(amount, false, true, EXPType::XP_GENERIC);

			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("BoughtExp"));

			success = true;
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("NoPoints"));
		}

		return success;
	}

	bool Buy(AShooterPlayerController* player_controller, const FString& item_id, int amount)
	{
		if (AsaApi::IApiUtils::IsPlayerDead(player_controller))
		{
			return false;
		}

		if (amount <= 0)
		{
			amount = 1;
		}

		bool success = false;

		const FString& eos_id = AsaApi::IApiUtils::GetEOSIDFromController(player_controller);

		if (DBHelper::IsPlayerExists(eos_id))
		{
			auto items_list = config["ShopItems"];

			auto item_entry_iter = items_list.find(item_id.ToString());
			if (item_entry_iter == items_list.end())
			{
				AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
					*GetText("WrongId"));
				return false;
			}

			auto item_entry = item_entry_iter.value();

			const std::string type = item_entry.value("Type", "");

			// Check if player has permisson to buy this

			const std::string permissions = item_entry.value("Permissions", "");
			if (!permissions.empty())
			{
				const FString fpermissions(permissions);

				TArray<FString> groups;
				fpermissions.ParseIntoArray(groups, L",", true);

				bool has_permissions = false;

				for (const auto& group : groups)
				{
					if (Permissions::IsPlayerInGroup(eos_id, group))
					{
						has_permissions = true;
						break;
					}
				}

				if (!has_permissions)
				{
					AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
						*GetText("NoPermissionsStore"), type);
					return false;
				}
			}

			const int min_level = item_entry.value("MinLevel", 1);
			const int max_level = item_entry.value("MaxLevel", 999);

			auto* primal_character = static_cast<APrimalCharacter*>(player_controller->GetPlayerCharacter());
			UPrimalCharacterStatusComponent* char_component = primal_character->MyCharacterStatusComponentField();

			const int level = char_component->BaseCharacterLevelField() + char_component->ExtraCharacterLevelField();
			if (level < min_level || level > max_level)
			{
				AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
					*GetText("BadLevel"), min_level, max_level);
				return false;
			}

			const unsigned price = item_entry["Price"];
			int final_price = price;
			if (type == "item")
			{
				success = BuyItem(player_controller, item_entry, eos_id, amount);
				if (success)
					final_price = price * amount;
			}
			else if (type == "dino")
			{
				success = BuyDino(player_controller, item_entry, eos_id);
			}
			else if (type == "beacon")
			{
				success = BuyBeacon(player_controller, item_entry, eos_id);
			}
			else if (type == "experience")
			{
				success = BuyExperience(player_controller, item_entry, eos_id);
			}
			else if (type == "unlockengram")
			{
				success = UnlockEngram(player_controller, item_entry, eos_id);
			}
			else if (type == "command")
			{
				success = BuyCommand(player_controller, item_entry, eos_id);
			}

			if (success)
			{
				const std::wstring log = fmt::format(TEXT("[{}] {}({}) Bought item: '{}' Amount: {} Total Spent Points: {}"),
					*ArkShop::SetMapName(),
					*AsaApi::IApiUtils::GetSteamName(player_controller),
					eos_id.ToString(),
					*item_id, amount,
					final_price);

				ShopLog::GetLog()->info(AsaApi::Tools::Utf8Encode(log));
				ArkShop::PostToDiscord(log);
			}
		}

		return success;
	}

	// Chat callbacks

	void ChatBuy(AShooterPlayerController* player_controller, FString* message, int, int senderPlatform)
	{
		if (!IsStoreEnabled(player_controller))
			return;

		if (ShouldPreventStoreUse(player_controller))
			return;

		TArray<FString> parsed;
		message->ParseIntoArray(parsed, L" ", true);

		if (parsed.IsValidIndex(1))
		{
			int amount = 0;

			if (parsed.IsValidIndex(2))
			{
				try
				{
					amount = std::stoi(*parsed[2]);
				}
				catch (const std::exception&)
				{
					return;
				}
			}

			Buy(player_controller, parsed[1], amount);
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("BuyUsage"));
		}
	}

	void ShowItems(AShooterPlayerController* player_controller, FString* message, int, int senderPlatform)
	{
		const FString& eos_id = AsaApi::GetApiUtils().GetEOSIDFromController(player_controller);

		if (!eos_id.IsEmpty() && AsaApi::Tools::IsPluginLoaded("ArkShopUI") && ArkShopUI::CanUseMod(eos_id))
		{
			if (ArkShopUI::RequestUI(eos_id))
			{
				int points = Points::GetPoints(eos_id);
				ArkShopUI::UpdatePoints(eos_id, points);

				ArkShop::Kits::InitKitData(eos_id);
				return;
			}
		}

		TArray<FString> parsed;
		message->ParseIntoArray(parsed, L" ", true);

		int page = 0;

		if (parsed.IsValidIndex(1))
		{
			try
			{
				page = std::stoi(*parsed[1]) - 1;
			}
			catch (const std::exception&)
			{
				return;
			}
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"),
				*GetText("ShopUsage"));
		}

		if (page < 0)
		{
			return;
		}

		auto items_list = config["ShopItems"];

		const int items_per_page = config["General"].value("ItemsPerPage", 20);
		const float display_time = config["General"].value("ShopDisplayTime", 15.0f);
		const float text_size = config["General"].value("ShopTextSize", 1.3f);

		const unsigned start_index = page * items_per_page;
		if (start_index >= items_list.size())
		{
			return;
		}

		auto start = items_list.begin();
		advance(start, start_index);

		FString store_str = "";

		for (auto iter = start; iter != items_list.end(); ++iter)
		{
			const size_t i = distance(items_list.begin(), iter);
			if (i == start_index + items_per_page)
			{
				break;
			}

			auto item = iter.value();

			const int price = item["Price"];
			const std::string type = item["Type"];
			const std::wstring description = AsaApi::Tools::Utf8Decode(item.value("Description", "No description"));

			if (type == "dino")
			{
				const int level = item["Level"];

				store_str += FString::Format(*GetText("StoreListDino"), i + 1, description, level,
					AsaApi::Tools::Utf8Decode(iter.key()), price);
			}
			else
			{
				store_str += FString::Format(*GetText("StoreListItem"), i + 1, description,
					AsaApi::Tools::Utf8Decode(iter.key()),
					price);
			}
		}

		store_str = FString::Format(*GetText("StoreListFormat"), *store_str);

		AsaApi::GetApiUtils().SendNotification(player_controller, FColorList::Green, text_size, display_time, nullptr,
			*store_str);

		FString shopmessage = GetText("ShopMessage");
		if (shopmessage != AsaApi::Tools::Utf8Decode("No message").c_str())
		{
			shopmessage = FString::Format(*shopmessage, page + 1,
				items_list.size() % items_per_page == 0
				? items_list.size() / items_per_page
				: items_list.size() / items_per_page + 1);
			AsaApi::GetApiUtils().SendNotification(player_controller, FColorList::Green, text_size, display_time,
				nullptr,
				*shopmessage);
		}
	}

	bool findCaseInsensitive(const std::wstring& data, const std::wstring& toSearch, size_t pos = 0)
	{
		std::wstring dataLower = data;
		std::wstring toSearchLower = toSearch;

		std::transform(dataLower.begin(), dataLower.end(), dataLower.begin(), ::towlower);
		std::transform(toSearchLower.begin(), toSearchLower.end(), toSearchLower.begin(), ::towlower);

		return dataLower.find(toSearchLower, pos) != std::wstring::npos;
	}

	void FindItems(AShooterPlayerController* player_controller, FString* message, int, int)
	{
		std::wstring searchTerm;
		TArray<FString> parsed;
		message->ParseIntoArray(parsed, L" ", true);

		if (parsed.IsValidIndex(1))
		{
			searchTerm = AsaApi::Tools::Utf8Decode(parsed[1].ToString());
		}
		else
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"), *GetText("ShopFindUsage"));
			return;
		}

		auto items_list = config["ShopItems"];

		const int items_per_page = config["General"].value("ItemsPerPage", 20);
		const float display_time = config["General"].value("ShopDisplayTime", 15.0f);
		const float text_size = config["General"].value("ShopTextSize", 1.3f);

		FString store_str = "";

		int count = 0;
		for (auto iter = items_list.begin(); iter != items_list.end(); ++iter)
		{
			bool found = false;

			const size_t i = distance(items_list.begin(), iter);

			auto item = iter.value();
			std::wstring key = AsaApi::Tools::Utf8Decode(iter.key());
			if (findCaseInsensitive(key, searchTerm))
				found = true;

			const int price = item["Price"];
			const std::string type = item["Type"];
			const std::wstring description = AsaApi::Tools::Utf8Decode(item.value("Description", "No description"));

			if (!found && findCaseInsensitive(description, searchTerm))
				found = true;

			if (found)
			{
				if (count >= items_per_page)
				{
					store_str += FString::Format(*GetText("ShopFindTooManyResults"));
					break;
				}
				if (type == "dino")
				{
					const int level = item["Level"];

					store_str += FString::Format(*GetText("StoreListDino"), i + 1, description, level,
						AsaApi::Tools::Utf8Decode(iter.key()), price);
				}
				else
				{
					store_str += FString::Format(*GetText("StoreListItem"), i + 1, description,
						AsaApi::Tools::Utf8Decode(iter.key()),
						price);
				}

				count++;
			}
		}

		if (store_str.IsEmpty())
		{
			AsaApi::GetApiUtils().SendChatMessage(player_controller, GetText("Sender"), *GetText("ShopFindNotFound"));
		}
		else
		{
			store_str = FString::Format(*GetText("StoreListFormat"), *store_str);

			AsaApi::GetApiUtils().SendNotification(player_controller, FColorList::Green, text_size, display_time, nullptr,
				*store_str);
		}
	}

	bool IsStoreEnabled(AShooterPlayerController* player_controller)
	{
		return ArkShop::IsStoreEnabled(player_controller);
	}

	void ToogleStore(bool enabled, const FString& reason)
	{
		ArkShop::ToogleStore(enabled, reason);
	}

	void Init()
	{
		auto& commands = AsaApi::GetCommands();

		commands.AddChatCommand(GetText("BuyCmd"), &ChatBuy);
		commands.AddChatCommand(GetText("ShopCmd"), &ShowItems);
		commands.AddChatCommand(GetText("ShopFindCmd"), &FindItems);
	}

	void Unload()
	{
		auto& commands = AsaApi::GetCommands();

		commands.RemoveChatCommand(GetText("BuyCmd"));
		commands.RemoveChatCommand(GetText("ShopCmd"));
		commands.RemoveChatCommand(GetText("ShopFindCmd"));
	}
} // namespace Store // namespace ArkShop