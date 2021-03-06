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

#include "src/importance/ActivityObserverUtils.h"
#include "tests/test/nodeps/NumericTestUtils.h"
#include "tests/test/plugins/AccountObserverTestContext.h"
#include "tests/TestHarness.h"

namespace catapult { namespace importance {

#define TEST_CLASS ActivityObserverUtilsTests

	namespace {
		constexpr auto Harvesting_Mosaic_Id = MosaicId(987);
		constexpr auto Notification_Height = Height(100);
		constexpr auto Importance_Height = model::ImportanceHeight(98);

		// region test context

		class TestContext : public test::AccountObserverTestContext {
		public:
			TestContext(observers::NotifyMode notifyMode, Amount minHarvesterBalance)
					: test::AccountObserverTestContext(notifyMode, Notification_Height, CreateBlockChainConfiguration(minHarvesterBalance))
			{}

		public:
			auto addAccount(const Key& publicKey, Amount harvestingBalance) {
				auto& accountStateCache = cache().sub<cache::AccountStateCache>();
				accountStateCache.addAccount(publicKey, Height(123));

				auto accountStateIter = accountStateCache.find(publicKey);
				accountStateIter.get().Balances.credit(Harvesting_Mosaic_Id, harvestingBalance);
				return accountStateIter;
			}

		public:
			void update(const Key& publicKey) {
				auto commitAction = [](auto& bucket) {
					bucket.BeneficiaryCount += 2;
				};
				auto rollbackAction = [](auto& bucket) {
					bucket.BeneficiaryCount -= 2;
				};

				UpdateActivity(publicKey, observerContext(), commitAction, rollbackAction);
			}

		private:
			static model::BlockChainConfiguration CreateBlockChainConfiguration(Amount minHarvesterBalance) {
				auto config = model::BlockChainConfiguration::Uninitialized();
				config.HarvestingMosaicId = Harvesting_Mosaic_Id;
				config.ImportanceGrouping = 2;
				config.MinHarvesterBalance = minHarvesterBalance;
				return config;
			}
		};

		// endregion
	}

	// region eligibility check

	namespace {
		void AssertUpdateActivityBypassesUpdateOfAccountThatCannotHarvest(observers::NotifyMode notifyMode) {
			// Arrange:
			TestContext context(notifyMode, Amount(1000));
			auto signerPublicKey = test::GenerateRandomByteArray<Key>();
			auto signerAccountStateIter = context.addAccount(signerPublicKey, Amount(999));

			// Act:
			context.update(signerPublicKey);

			// Assert: no bucket was created
			const auto& activityBucket = signerAccountStateIter.get().ActivityBuckets.get(Importance_Height);
			EXPECT_EQ(model::ImportanceHeight(), activityBucket.StartHeight);
		}
	}

	TEST(TEST_CLASS, UpdateActivityBypassesUpdateOfAccountThatCannotHarvest_Commit) {
		AssertUpdateActivityBypassesUpdateOfAccountThatCannotHarvest(observers::NotifyMode::Commit);
	}

	TEST(TEST_CLASS, UpdateActivityBypassesUpdateOfAccountThatCannotHarvest_Rollback) {
		AssertUpdateActivityBypassesUpdateOfAccountThatCannotHarvest(observers::NotifyMode::Rollback);
	}

	// endregion

	// region basic update

	namespace {
		void AssertUpdateActivityUpdatesExistingBucket(observers::NotifyMode notifyMode, uint32_t expectedBeneficiaryCount) {
			// Arrange:
			TestContext context(notifyMode, Amount(1000));
			auto signerPublicKey = test::GenerateRandomByteArray<Key>();
			auto signerAccountStateIter = context.addAccount(signerPublicKey, Amount(1000));
			signerAccountStateIter.get().ActivityBuckets.update(Importance_Height, [](auto& bucket) {
				bucket.BeneficiaryCount = 100;
			});

			// Act:
			context.update(signerPublicKey);

			// Assert: bucket was updated
			const auto& activityBucket = signerAccountStateIter.get().ActivityBuckets.get(Importance_Height);
			EXPECT_EQ(Importance_Height, activityBucket.StartHeight);
			EXPECT_EQ(expectedBeneficiaryCount, activityBucket.BeneficiaryCount);
		}
	}

	TEST(TEST_CLASS, UpdateActivityUpdatesExistingBucket_Commit) {
		AssertUpdateActivityUpdatesExistingBucket(observers::NotifyMode::Commit, 102);
	}

	TEST(TEST_CLASS, UpdateActivityUpdatesExistingBucket_Rollback) {
		AssertUpdateActivityUpdatesExistingBucket(observers::NotifyMode::Rollback, 98);
	}

	// endregion

	// region bucket creation

	TEST(TEST_CLASS, UpdateActivityCommitCreatesNewBucket) {
		// Arrange:
		TestContext context(observers::NotifyMode::Commit, Amount(1000));
		auto signerPublicKey = test::GenerateRandomByteArray<Key>();
		auto signerAccountStateIter = context.addAccount(signerPublicKey, Amount(1000));

		// Act:
		context.update(signerPublicKey);

		// Assert: bucket was created
		const auto& activityBucket = signerAccountStateIter.get().ActivityBuckets.get(Importance_Height);
		EXPECT_EQ(Importance_Height, activityBucket.StartHeight);
		EXPECT_EQ(2u, activityBucket.BeneficiaryCount);
	}

	TEST(TEST_CLASS, UpdateActivityRollbackDoesNotCreateNewBucket) {
		// Arrange:
		TestContext context(observers::NotifyMode::Rollback, Amount(1000));
		auto signerPublicKey = test::GenerateRandomByteArray<Key>();
		auto signerAccountStateIter = context.addAccount(signerPublicKey, Amount(1000));

		// Act:
		context.update(signerPublicKey);

		// Assert: bucket was not created
		const auto& activityBucket = signerAccountStateIter.get().ActivityBuckets.get(Importance_Height);
		EXPECT_EQ(model::ImportanceHeight(), activityBucket.StartHeight);
	}

	// endregion

	// region bucket removal

	TEST(TEST_CLASS, UpdateActivityCommitDoesNotRemoveZeroBucket) {
		// Arrange:
		TestContext context(observers::NotifyMode::Commit, Amount(1000));
		auto signerPublicKey = test::GenerateRandomByteArray<Key>();
		auto signerAccountStateIter = context.addAccount(signerPublicKey, Amount(1000));
		signerAccountStateIter.get().ActivityBuckets.update(Importance_Height, [](auto& bucket) {
			test::SetMaxValue(bucket.BeneficiaryCount);
			--bucket.BeneficiaryCount;
		});

		// Act:
		context.update(signerPublicKey);

		// Assert: bucket was updated
		const auto& activityBucket = signerAccountStateIter.get().ActivityBuckets.get(Importance_Height);
		EXPECT_EQ(Importance_Height, activityBucket.StartHeight);
		EXPECT_EQ(0u, activityBucket.BeneficiaryCount);
	}

	TEST(TEST_CLASS, UpdateActivityRollbackRemovesZeroBucket) {
		// Arrange:
		TestContext context(observers::NotifyMode::Rollback, Amount(1000));
		auto signerPublicKey = test::GenerateRandomByteArray<Key>();
		auto signerAccountStateIter = context.addAccount(signerPublicKey, Amount(1000));
		signerAccountStateIter.get().ActivityBuckets.update(Importance_Height, [](auto& bucket) {
			bucket.BeneficiaryCount = 2;
		});

		// Act:
		context.update(signerPublicKey);

		// Assert: bucket was removed
		const auto& activityBucket = signerAccountStateIter.get().ActivityBuckets.get(Importance_Height);
		EXPECT_EQ(model::ImportanceHeight(), activityBucket.StartHeight);
	}

	namespace {
		template<typename TUpdateBucket>
		void AssertUpdateActivityRollbackDoesNotRemoveNonzeroBucket(const char* message, TUpdateBucket updateBucket) {
			// Arrange:
			TestContext context(observers::NotifyMode::Rollback, Amount(1000));
			auto signerPublicKey = test::GenerateRandomByteArray<Key>();
			auto signerAccountStateIter = context.addAccount(signerPublicKey, Amount(1000));
			signerAccountStateIter.get().ActivityBuckets.update(Importance_Height, [updateBucket](auto& bucket) {
				bucket.BeneficiaryCount = 2;
				updateBucket(bucket);
			});

			// Act:
			context.update(signerPublicKey);

			// Assert: bucket was updated
			const auto& activityBucket = signerAccountStateIter.get().ActivityBuckets.get(Importance_Height);
			EXPECT_EQ(Importance_Height, activityBucket.StartHeight) << message;
		}
	}

	TEST(TEST_CLASS, UpdateActivityRollbackDoesNotRemoveNonzeroBucket) {
		AssertUpdateActivityRollbackDoesNotRemoveNonzeroBucket("TotalFeesPaid", [](auto& bucket) {
			bucket.TotalFeesPaid = bucket.TotalFeesPaid + Amount(1);
		});
		AssertUpdateActivityRollbackDoesNotRemoveNonzeroBucket("BeneficiaryCount", [](auto& bucket) {
			++bucket.BeneficiaryCount;
		});
		AssertUpdateActivityRollbackDoesNotRemoveNonzeroBucket("RawScore", [](auto& bucket) {
			++bucket.RawScore;
		});
	}

	// endregion
}}
