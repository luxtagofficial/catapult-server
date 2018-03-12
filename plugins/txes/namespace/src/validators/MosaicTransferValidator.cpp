#include "Validators.h"
#include "ActiveMosaicView.h"
#include "src/cache/MosaicCache.h"
#include "catapult/cache/ReadOnlyCatapultCache.h"
#include "catapult/cache_core/AccountStateCache.h"
#include "catapult/validators/ValidatorContext.h"
#include "catapult/constants.h"

namespace catapult { namespace validators {

	using Notification = model::BalanceTransferNotification;

	namespace {
		bool IsMosaicOwnerParticipant(const cache::ReadOnlyCatapultCache& cache, const Key& owner, const Notification& notification) {
			if (owner == notification.Sender)
				return true;

			// the owner must exist if the mosaic lookup succeeded
			const auto& accountStateCache = cache.sub<cache::AccountStateCache>();
			const auto& ownerAccountState = accountStateCache.get(owner);
			return ownerAccountState.Address == notification.Recipient;
		}
	}

	DEFINE_STATEFUL_VALIDATOR(MosaicTransfer, [](const auto& notification, const auto& context) {
		// 0. whitelist xem
		if (Xem_Id == notification.MosaicId)
			return ValidationResult::Success;

		// 1. check that the mosaic exists
		const state::MosaicEntry* pEntry;
		ActiveMosaicView activeMosaicView(context.Cache);
		auto result = activeMosaicView.tryGet(notification.MosaicId, context.Height, &pEntry);
		if (!IsValidationResultSuccess(result))
			return result;

		// 2. if it's transferable there's nothing else to check
		if (pEntry->definition().properties().is(model::MosaicFlags::Transferable))
			return ValidationResult::Success;

		// 3. if it's NOT transferable then owner must be either sender or recipient
		if (!IsMosaicOwnerParticipant(context.Cache, pEntry->definition().owner(), notification))
			return Failure_Mosaic_Non_Transferable;

		return ValidationResult::Success;
	});
}}