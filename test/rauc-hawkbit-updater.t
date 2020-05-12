#!/bin/sh

test_description="rauc-hawkbit-updater binary tests"


: "${SHARNESS_TEST_SRCDIR:=.}"
. "$SHARNESS_TEST_SRCDIR/sharness.sh"

SHARNESS_BUILD_DIRECTORY=$SHARNESS_TEST_DIRECTORY/../build

VALID_TOKEN=bhVahL1Il1shie2aj2poojeChee6ahShu
INVALID_TOKEN=ahVahL1Il1shie2aj2poojeChee6ahShu

test_expect_success "rauc-hawkbit-updater version" "
  rauc-hawkbit-updater -v
"

test_expect_success "rauc-hawkbit-updater invalid argument" "
  test_expect_code 1 rauc-hawkbit-updater --invalidarg
"

test_expect_success "rauc-hawkbit-updater no config file" "
  test_expect_code 2 rauc-hawkbit-updater
"

test_expect_success "rauc-hawkbit-updater non-existing config file given" "
  test_expect_code 3 rauc-hawkbit-updater -c does-not-exist.conf
"

test_expect_success "rauc-hawkbit-updater register and check (valid auth token)" "
  $SHARNESS_TEST_DIRECTORY/create_test_target && \
  cp $SHARNESS_TEST_DIRECTORY/test-config.conf . && \
  sed -i s/TEST_TOKEN/$VALID_TOKEN/g test-config.conf && \
  rauc-hawkbit-updater -c test-config.conf -r
"

test_expect_success "rauc-hawkbit-updater register and check (invalid auth token)" "
  $SHARNESS_TEST_DIRECTORY/create_test_target && \
  cp $SHARNESS_TEST_DIRECTORY/test-config.conf . && \
  sed -i s/TEST_TOKEN/$INVALID_TOKEN/g test-config.conf && \
  test_must_fail rauc-hawkbit-updater -c $SHARNESS_TEST_DIRECTORY/test-config.conf -r
"

test_expect_success "rauc-hawkbit-updater register and check (invalid gateway token, no auth token)" "
  $SHARNESS_TEST_DIRECTORY/create_test_target &&
  cp $SHARNESS_TEST_DIRECTORY/test-config-gateway-token-only.conf . &&
  sed -i s/TEST_TOKEN/$INVALID_TOKEN/g test-config-gateway-token-only.conf &&
  echo 'MESSAGE: Checking for new software...\nWARNING: Failed to authenticate. Check if gateway_token is correct?' > expected_out &&
  test_expect_code 1 rauc-hawkbit-updater -r -c $SHARNESS_TEST_DIRECTORY/test-config-gateway-token-only.conf > actual_out 2>&1 &&
  test_cmp expected_out actual_out
"

test_expect_success "rauc-hawkbit-updater register and check (invalid auth token, no gateway token)" "
  $SHARNESS_TEST_DIRECTORY/create_test_target &&
  cp $SHARNESS_TEST_DIRECTORY/test-config-auth-token-only.conf . &&
  sed -i s/TEST_TOKEN/$INVALID_TOKEN/g test-config-auth-token-only.conf &&
  echo 'MESSAGE: Checking for new software...\nWARNING: Failed to authenticate. Check if auth_token is correct?' > expected_out &&
  test_expect_code 1 rauc-hawkbit-updater -r -c $SHARNESS_TEST_DIRECTORY/test-config-auth-token-only.conf > actual_out 2>&1 &&
  test_cmp expected_out actual_out
"

test_expect_success "rauc-hawkbit-updater register and check (no security tokens)" "
  cp $SHARNESS_TEST_DIRECTORY/test-config-no-security-tokens.conf . &&
  echo 'Loading config file failed: Neither a token nor client certificate are set!' > expected_out &&
  test_expect_code 4 rauc-hawkbit-updater -r -c $SHARNESS_TEST_DIRECTORY/test-config-no-security-tokens.conf > actual_out 2>&1 &&
  test_cmp expected_out actual_out
"

test_expect_success "rauc-hawkbit-updater register and check (both security tokens, both invalid)" "
  $SHARNESS_TEST_DIRECTORY/create_test_target &&
  cp $SHARNESS_TEST_DIRECTORY/test-config-both-security-tokens.conf . &&
  sed -i s/TEST_TOKEN/$INVALID_TOKEN/g test-config-both-security-tokens.conf &&
  echo 'Loading config file failed: Both auth_token and gateway_token are set in the config.' > expected_out &&
  test_expect_code 4 rauc-hawkbit-updater -r -c $SHARNESS_TEST_DIRECTORY/test-config-both-security-tokens.conf > actual_out 2>&1 &&
  test_cmp expected_out actual_out
"

test_done
