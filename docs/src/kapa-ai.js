(function () {
	window.addEventListener('load', function () {
		const activeGroups = window.OnetrustActiveGroups || '';
		if (activeGroups.indexOf('C0002') > -1) {
			const askAiTop = document.querySelector('.ask-ai-top');
			const askAiTopSpan = document.querySelector('.ask-ai-top > span');
			const kapaButton = document.querySelector('.yb-kapa-button');

			if (askAiTopSpan) {
				askAiTopSpan.addEventListener('click', function () {
					if (window.Kapa) {
						window.Kapa.open({
							mode: 'ai',
						});
					}
				});
			}

			if (askAiTop) {
				askAiTop.classList.remove('hidden');
			}

			if (kapaButton) {
				kapaButton.classList.remove('hidden');
			}
		}

		let clearbitAnonymousId = window.browserCookieUtils.getCookie('cb_anonymous_id');
		if (clearbitAnonymousId && clearbitAnonymousId !== 'null') {
			clearbitAnonymousId = clearbitAnonymousId.replace(/%22/g, '').replace(/"/g, '');
			const kapaUser = {
				uniqueClientId: clearbitAnonymousId,
			};

			let clearbitUserId = window.browserCookieUtils.getCookie('cb_user_id');
			if (clearbitUserId && clearbitUserId !== 'null') {
				clearbitUserId = clearbitUserId.replace(/%22/g, '').replace(/"/g, '');
				kapaUser.email = clearbitUserId;
			}

			window.kapaSettings = {
				user: kapaUser,
			};
		}
	});
})();
