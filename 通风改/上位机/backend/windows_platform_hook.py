"""Install bounded metadata queries before third-party frozen startup hooks."""
from backend.windows_platform import install_bounded_platform_queries

install_bounded_platform_queries()
