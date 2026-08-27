use Test::Nginx::Socket 'no_plan';

no_root_location();
no_shuffle();

run_tests();

__DATA__

=== TEST 1: dummy handlers run in priority order independent of load_module order
--- config
include $TEST_NGINX_CONF_DIR/location.conf;
--- request
GET /
--- response_headers
X-Nxe-Phase-Order: a,b
--- error_code: 200
