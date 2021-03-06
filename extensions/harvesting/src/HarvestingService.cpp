/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#include "HarvestingService.h"
#include "HarvesterBlockGenerator.h"
#include "HarvestingUtFacadeFactory.h"
#include "ScheduledHarvesterTask.h"
#include "UnlockedAccounts.h"
#include "UnlockedAccountsUpdater.h"
#include "catapult/cache_core/ImportanceView.h"
#include "catapult/cache_tx/MemoryUtCache.h"
#include "catapult/crypto/KeyUtils.h"
#include "catapult/extensions/ConfigurationUtils.h"
#include "catapult/extensions/ExecutionConfigurationFactory.h"
#include "catapult/extensions/ServiceLocator.h"
#include "catapult/extensions/ServiceState.h"
#include "catapult/io/BlockStorageCache.h"
#include "catapult/plugins/PluginManager.h"

namespace catapult { namespace harvesting {

	namespace {
		std::shared_ptr<UnlockedAccounts> CreateUnlockedAccounts(
				const HarvestingConfiguration& config,
				const cache::CatapultCache& cache) {
			auto unlockedAccountsFactory = [&config, &cache](const auto& primaryAccountPublicKey) {
				return std::make_shared<UnlockedAccounts>(
						config.MaxUnlockedAccounts,
						CreateDelegatePrioritizer(config.DelegatePrioritizationPolicy, cache, primaryAccountPublicKey));
			};

			if (!config.EnableAutoHarvesting)
				return unlockedAccountsFactory(Key());

			auto harvesterKeyPair = crypto::KeyPair::FromString(config.HarvesterPrivateKey);
			auto harvesterPublicKey = harvesterKeyPair.publicKey();
			auto pUnlockedAccounts = unlockedAccountsFactory(harvesterPublicKey);

			// unlock configured account if it's eligible to harvest the next block
			auto unlockResult = pUnlockedAccounts->modifier().add(std::move(harvesterKeyPair));
			CATAPULT_LOG(info)
					<< "Unlocked harvesting account " << harvesterPublicKey
					<< " for harvesting with result " << unlockResult;

			return pUnlockedAccounts;
		}

		ScheduledHarvesterTaskOptions CreateHarvesterTaskOptions(extensions::ServiceState& state) {
			ScheduledHarvesterTaskOptions options;
			options.HarvestingAllowed = state.hooks().chainSyncedPredicate();
			options.LastBlockElementSupplier = [&storage = state.storage()]() {
				auto storageView = storage.view();
				return storageView.loadBlockElement(storageView.chainHeight());
			};
			options.TimeSupplier = state.timeSupplier();
			options.RangeConsumer = state.hooks().completionAwareBlockRangeConsumerFactory()(disruptor::InputSource::Local);
			return options;
		}

		thread::Task CreateHarvestingTask(
				extensions::ServiceState& state,
				UnlockedAccounts& unlockedAccounts,
				const crypto::KeyPair& bootKeyPair,
				const Key& beneficiaryPublicKey) {
			const auto& cache = state.cache();
			const auto& blockChainConfig = state.config().BlockChain;
			const auto& utCache = state.utCache();
			auto strategy = state.config().Node.TransactionSelectionStrategy;
			auto executionConfig = extensions::CreateExecutionConfiguration(state.pluginManager());
			HarvestingUtFacadeFactory utFacadeFactory(cache, blockChainConfig, executionConfig);

			auto pUnlockedAccountsUpdater = std::make_shared<UnlockedAccountsUpdater>(
					cache,
					unlockedAccounts,
					bootKeyPair,
					config::CatapultDataDirectory(state.config().User.DataDirectory));
			pUnlockedAccountsUpdater->load();

			auto blockGenerator = CreateHarvesterBlockGenerator(strategy, utFacadeFactory, utCache);
			auto pHarvesterTask = std::make_shared<ScheduledHarvesterTask>(
					CreateHarvesterTaskOptions(state),
					std::make_unique<Harvester>(cache, blockChainConfig, beneficiaryPublicKey, unlockedAccounts, blockGenerator));

			return thread::CreateNamedTask("harvesting task", [pUnlockedAccountsUpdater, pHarvesterTask]() {
				pUnlockedAccountsUpdater->update();

				// harvest the next block
				pHarvesterTask->harvest();
				return thread::make_ready_future(thread::TaskResult::Continue);
			});
		}

		class HarvestingServiceRegistrar : public extensions::ServiceRegistrar {
		public:
			explicit HarvestingServiceRegistrar(const HarvestingConfiguration& config) : m_config(config)
			{}

			extensions::ServiceRegistrarInfo info() const override {
				return { "Harvesting", extensions::ServiceRegistrarPhase::Post_Range_Consumers };
			}

			void registerServiceCounters(extensions::ServiceLocator& locator) override {
				locator.registerServiceCounter<UnlockedAccounts>("unlockedAccounts", "UNLKED ACCTS", [](const auto& accounts) {
					return accounts.view().size();
				});
			}

			void registerServices(extensions::ServiceLocator& locator, extensions::ServiceState& state) override {
				auto pUnlockedAccounts = CreateUnlockedAccounts(m_config, state.cache());
				locator.registerRootedService("unlockedAccounts", pUnlockedAccounts);

				// add tasks
				auto beneficiaryPublicKey = crypto::ParseKey(m_config.BeneficiaryPublicKey);
				state.tasks().push_back(CreateHarvestingTask(state, *pUnlockedAccounts, locator.keyPair(), beneficiaryPublicKey));
			}

		private:
			HarvestingConfiguration m_config;
		};
	}

	DECLARE_SERVICE_REGISTRAR(Harvesting)(const HarvestingConfiguration& config) {
		return std::make_unique<HarvestingServiceRegistrar>(config);
	}
}}
